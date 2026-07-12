// Personal Finance Hub - In-Memory Account Repository
// Version: 1.0
// C++23
//
// Enforces:
// - user isolation on ownership-sensitive reads
// - optimistic locking via Account.version
// - balance cache hit/miss/rebuild using BalanceCalculationService

#pragma once

#include "pfh/domain/balance_calculation_service.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include <map>
#include <set>
#include <utility>

namespace pfh::infrastructure {

class InMemoryAccountRepository final : public domain::IAccountRepository {
public:
    InMemoryAccountRepository(
        InMemoryStore& store,
        domain::ITransactionRepository& transaction_repo)
        : store_(store), transaction_repo_(transaction_repo) {}

    [[nodiscard]] domain::RepositoryResult<domain::Account> find_by_id(
        domain::AccountId id) override {
        if (is_staged_deleted_account(id.value())) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Account not found: " + id.to_string()));
        }
        if (auto it = store_.staged_accounts.find(id.value());
            store_.in_transaction && it != store_.staged_accounts.end()) {
            return it->second;
        }
        if (auto it = store_.accounts.find(id.value()); it != store_.accounts.end()) {
            return it->second;
        }
        return std::unexpected(domain::RepositoryError::not_found(
            "Account not found: " + id.to_string()));
    }

    [[nodiscard]] domain::RepositoryResult<domain::Account> find_by_id_for_user(
        domain::AccountId id,
        domain::UserId user_id) override {
        auto account = find_by_id(id);
        if (!account.has_value()) {
            return account;
        }
        if (account->owner() != user_id) {
            // Do not leak existence across users.
            return std::unexpected(domain::RepositoryError::not_found(
                "Account not found for user"));
        }
        return account;
    }

    [[nodiscard]] domain::RepositoryResult<domain::Account> find_by_id_for_update(
        domain::ITransactionContext& /*tx*/,
        domain::AccountId id,
        domain::UserId user_id) override {
        // The in-memory store is single-threaded, so there is no real row lock
        // to take; the transaction requirement and ownership semantics still
        // match the PostgreSQL `SELECT ... FOR UPDATE` contract callers rely on.
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "find_by_id_for_update requires an active transaction"));
        }
        return find_by_id_for_user(id, user_id);
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Account>> find_active_by_user(
        domain::UserId user_id) override {
        std::map<std::int64_t, domain::Account> merged = store_.accounts;
        if (store_.in_transaction) {
            for (const auto& [id, acc] : store_.staged_accounts) {
                merged.insert_or_assign(id, acc);
            }
            for (const auto id : store_.staged_deleted_accounts) {
                merged.erase(id);
            }
        }

        std::vector<domain::Account> result;
        for (const auto& [_, acc] : merged) {
            if (acc.owner() == user_id && !acc.is_archived()) {
                result.push_back(acc);
            }
        }
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Currency>> find_active_currencies()
        override {
        std::map<std::int64_t, domain::Account> merged = store_.accounts;
        if (store_.in_transaction) {
            for (const auto& [id, acc] : store_.staged_accounts) {
                merged.insert_or_assign(id, acc);
            }
            for (const auto id : store_.staged_deleted_accounts) {
                merged.erase(id);
            }
        }

        std::set<std::string> seen;
        std::vector<domain::Currency> result;
        for (const auto& [_, acc] : merged) {
            if (acc.is_archived()) {
                continue;
            }
            const auto& code = acc.currency().code();
            if (seen.insert(code).second) {
                result.push_back(acc.currency());
            }
        }
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<domain::BalanceSnapshot> balance_of(
        domain::AccountId id) override {
        auto account = find_by_id(id);
        if (!account.has_value()) {
            return std::unexpected(account.error());
        }

        // Compute current source version from non-deleted transactions.
        auto txs_result = transaction_repo_.find_by_account(id, std::nullopt, std::nullopt, false);
        if (!txs_result.has_value()) {
            return std::unexpected(txs_result.error());
        }
        const auto& txs = *txs_result;

        std::int64_t source_version = 0;
        domain::TransactionId last_tx_id;
        for (const auto& tx : txs) {
            source_version += 1;
            last_tx_id = tx.id();
        }

        // Cache hit path.
        const InMemoryBalanceCache* cache = nullptr;
        if (store_.in_transaction) {
            if (auto it = store_.staged_balance_cache.find(id.value());
                it != store_.staged_balance_cache.end()) {
                cache = &it->second;
            }
        }
        if (cache == nullptr) {
            if (auto it = store_.balance_cache.find(id.value());
                it != store_.balance_cache.end()) {
                cache = &it->second;
            }
        }
        if (cache != nullptr && cache->source_version == source_version) {
            return domain::BalanceSnapshot(
                id,
                cache->balance,
                std::chrono::system_clock::now(),
                cache->last_transaction_id);
        }

        // Cache miss / invalid: rebuild via domain service.
        auto snapshot = domain::BalanceCalculationService::calculate_balance(
            id, txs, account->currency());
        if (!snapshot.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Balance calculation failed: " + snapshot.error().message));
        }
        // Domain service does not track the last transaction id; the repository
        // owns cache metadata, so stamp it here for the response DTO.
        snapshot->last_transaction_id = last_tx_id;

        // Write-back cache (staged if in transaction, otherwise direct).
        InMemoryBalanceCache rebuilt{
            snapshot->balance,
            last_tx_id,
            source_version,
            cache != nullptr ? cache->cache_version + 1 : 1};
        if (store_.in_transaction) {
            store_.staged_balance_cache.insert_or_assign(id.value(), std::move(rebuilt));
        } else {
            store_.balance_cache.insert_or_assign(id.value(), std::move(rebuilt));
        }
        return *snapshot;
    }

    [[nodiscard]] domain::RepositoryResult<domain::AccountId> save(
        domain::ITransactionContext& /*tx*/,
        const domain::Account& account) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "save requires an active transaction"));
        }

        // Create path: invalid id means assign new id.
        if (!account.id().is_valid()) {
            const auto id_value = store_.next_account_id++;
            domain::Account created(
                domain::AccountId(id_value),
                account.owner(),
                account.name(),
                account.type(),
                account.subtype(),
                account.currency(),
                account.description(),
                account.is_archived(),
                account.archived_at(),
                account.created_at(),
                account.updated_at(),
                1,
                account.has_category_override()
                    ? std::optional<domain::AccountCategory>(account.category())
                    : std::nullopt);
            store_.staged_accounts.emplace(id_value, std::move(created));
            return domain::AccountId(id_value);
        }

        // Update path with optimistic locking.
        const auto id = account.id().value();
        domain::Account existing = account;
        bool found = false;
        if (auto it = store_.staged_accounts.find(id); it != store_.staged_accounts.end()) {
            existing = it->second;
            found = true;
        } else if (auto it = store_.accounts.find(id); it != store_.accounts.end()) {
            existing = it->second;
            found = true;
        }
        if (!found) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Cannot update unknown account: " + account.id().to_string()));
        }

        // Caller must pass the current version; we compare against stored version.
        if (account.version() != existing.version()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Optimistic lock failure on account " + account.id().to_string() +
                ": expected version " + std::to_string(existing.version()) +
                ", got " + std::to_string(account.version())));
        }

        // Persist updated account with incremented version.
        domain::Account updated(
            account.id(),
            account.owner(),
            account.name(),
            account.type(),
            account.subtype(),
            account.currency(),
            account.description(),
            account.is_archived(),
            account.archived_at(),
            account.created_at(),
            account.updated_at(),
            account.version() + 1,
            account.has_category_override()
                ? std::optional<domain::AccountCategory>(account.category())
                : std::nullopt);
        store_.staged_accounts.insert_or_assign(id, std::move(updated));
        return account.id();
    }

    [[nodiscard]] domain::RepositoryVoidResult delete_balance_cache(
        domain::ITransactionContext& /*tx*/,
        domain::AccountId id) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "delete_balance_cache requires an active transaction"));
        }
        store_.staged_balance_cache.erase(id.value());
        store_.staged_deleted_balance_cache.push_back(id.value());
        return {};
    }

    [[nodiscard]] domain::RepositoryVoidResult physical_delete(
        domain::ITransactionContext& /*tx*/,
        domain::AccountId id) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "physical_delete requires an active transaction"));
        }
        store_.staged_accounts.erase(id.value());
        store_.staged_deleted_accounts.push_back(id.value());
        return {};
    }

private:
    [[nodiscard]] bool is_staged_deleted_account(std::int64_t id) const {
        if (!store_.in_transaction) {
            return false;
        }
        for (const auto deleted : store_.staged_deleted_accounts) {
            if (deleted == id) {
                return true;
            }
        }
        return false;
    }

    InMemoryStore& store_;
    domain::ITransactionRepository& transaction_repo_;
};

} // namespace pfh::infrastructure
