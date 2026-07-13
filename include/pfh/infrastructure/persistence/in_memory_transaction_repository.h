// Personal Finance Hub - In-Memory Transaction Repository
// Version: 1.0
// C++23
//
// Enforces TransferAggregate atomic write:
// transfer_groups + outgoing + incoming (+ adjustments) in one transaction.

#pragma once

#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace pfh::infrastructure {

class InMemoryTransactionRepository final : public domain::ITransactionRepository {
public:
    explicit InMemoryTransactionRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<domain::Transaction> find_by_id(
        domain::TransactionId id) override {
        if (is_staged_deleted_tx(id.value())) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found: " + id.to_string()));
        }
        if (store_.in_transaction) {
            if (auto it = store_.staged_transactions.find(id.value());
                it != store_.staged_transactions.end()) {
                return it->second;
            }
        }
        if (auto it = store_.transactions.find(id.value()); it != store_.transactions.end()) {
            return it->second;
        }
        return std::unexpected(domain::RepositoryError::not_found(
            "Transaction not found: " + id.to_string()));
    }

    [[nodiscard]] domain::RepositoryResult<domain::Transaction> find_by_id_for_update(
        domain::ITransactionContext& /*tx*/,
        domain::TransactionId id) override {
        // Single-threaded in-memory store: no real row lock to take, but the
        // active-transaction requirement mirrors the PostgreSQL FOR UPDATE
        // contract callers depend on for check-then-write serialization.
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "find_by_id_for_update requires an active transaction"));
        }
        return find_by_id(id);
    }

    [[nodiscard]] domain::RepositoryResult<domain::Transaction> save_single(
        domain::ITransactionContext& /*tx*/,
        const domain::Transaction& transaction) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "save_single requires an active transaction"));
        }

        // Transfer legs may ONLY be written via save_transfer (as a validated
        // aggregate). Reject standalone Transfer rows so no orphan leg can be
        // created that bypasses the aggregate consistency rules.
        if (transaction.type() == domain::TransactionType::Transfer) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer transactions must be written via save_transfer, not save_single"));
        }
        if (!transaction.amount().amount().fits_numeric_20_8()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transaction amount does not fit NUMERIC(20,8)"));
        }

        // Ownership and currency isolation: every transaction amount is
        // denominated in the account's own currency.
        if (auto account_check = ensure_account_matches_transaction(transaction);
            !account_check.has_value()) {
            return std::unexpected(account_check.error());
        }
        if (auto category_check = ensure_category_matches_transaction(transaction);
            !category_check.has_value()) {
            return std::unexpected(category_check.error());
        }

        auto id_value = transaction.id().is_valid()
            ? transaction.id().value()
            : store_.next_transaction_id++;
        if (transaction.id().is_valid() && id_value >= store_.next_transaction_id) {
            store_.next_transaction_id = id_value + 1;
        }

        // Normalize storage sign for domain-constructed positive magnitudes.
        // If amount is already signed (e.g. loaded then re-saved), keep it.
        domain::Money storage_amount = transaction.amount();
        if (transaction.type() == domain::TransactionType::Expense &&
            storage_amount.is_positive()) {
            storage_amount = storage_amount.negated();
        } else if (transaction.type() == domain::TransactionType::Income &&
                   storage_amount.is_negative()) {
            // Income should not be negative; reject.
            return std::unexpected(domain::RepositoryError::validation(
                "Income amount must not be negative"));
        }

        domain::Transaction stored(
            domain::TransactionId(id_value),
            transaction.user_id(),
            transaction.account_id(),
            storage_amount,
            transaction.type(),
            transaction.occurred_at(),
            transaction.description(),
            transaction.category_id(),
            transaction.transfer_group_id(),
            transaction.created_at(),
            transaction.deleted_at());
        store_.staged_transactions.insert_or_assign(id_value, stored);

        // Invalidate balance cache for this account (rebuild on next read).
        store_.staged_balance_cache.erase(transaction.account_id().value());
        store_.staged_deleted_balance_cache.push_back(transaction.account_id().value());
        return stored;
    }

    [[nodiscard]] domain::RepositoryResult<domain::TransferPersistResult> save_transfer(
        domain::ITransactionContext& tx,
        const domain::TransferAggregate& transfer) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "save_transfer requires an active transaction"));
        }

        const auto& outgoing = transfer.outgoing();
        const auto& incoming = transfer.incoming();

        if (!outgoing.amount().amount().fits_numeric_20_8() ||
            !incoming.amount().amount().fits_numeric_20_8()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer leg amount does not fit NUMERIC(20,8)"));
        }
        if (transfer.rate().has_value() &&
            !transfer.rate()->rate().fits_numeric_20_10()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer rate does not fit NUMERIC(20,10)"));
        }
        for (const auto& adjustment : transfer.adjustments()) {
            if (!adjustment.amount().amount().fits_numeric_20_8()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Transfer adjustment does not fit NUMERIC(20,8)"));
            }
            if (adjustment.type() != domain::TransactionType::Adjustment ||
                adjustment.user_id() != outgoing.user_id()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Transfer adjustments must be Adjustment rows owned by the transfer user"));
            }
            if (adjustment.transfer_group_id().has_value() &&
                adjustment.transfer_group_id()->is_valid() &&
                outgoing.transfer_group_id().has_value() &&
                outgoing.transfer_group_id()->is_valid() &&
                adjustment.transfer_group_id() != outgoing.transfer_group_id()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Transfer adjustment must share transfer_group_id"));
            }
            if (auto check = ensure_account_matches_transaction(adjustment);
                !check.has_value()) {
                return std::unexpected(check.error());
            }
        }

        if (outgoing.type() != domain::TransactionType::Transfer ||
            incoming.type() != domain::TransactionType::Transfer) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer sides must be TransactionType::Transfer"));
        }
        if (outgoing.user_id() != incoming.user_id()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer sides must belong to the same user"));
        }
        // transfer_group_id may be invalid (placeholder) at create time; repository assigns it.
        // If both sides provide a valid id, they must match.
        if (outgoing.transfer_group_id().has_value() &&
            incoming.transfer_group_id().has_value() &&
            outgoing.transfer_group_id()->is_valid() &&
            incoming.transfer_group_id()->is_valid() &&
            outgoing.transfer_group_id() != incoming.transfer_group_id()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer sides must share transfer_group_id"));
        }

        // Both accounts must exist, belong to the user, and their currency must
        // match the corresponding leg's currency (a leg's currency is fixed by
        // the account it lands on).
        if (auto check = ensure_account_matches_leg(outgoing);
            !check.has_value()) {
            return std::unexpected(check.error());
        }
        if (auto check = ensure_account_matches_leg(incoming);
            !check.has_value()) {
            return std::unexpected(check.error());
        }

        // Assign / reuse transfer group id.
        // Domain service may pass invalid id (0) as placeholder; repository assigns real id.
        std::int64_t group_value = 0;
        if (outgoing.transfer_group_id().has_value() &&
            outgoing.transfer_group_id()->is_valid()) {
            group_value = outgoing.transfer_group_id()->value();
        } else {
            group_value = store_.next_transfer_group_id++;
        }
        domain::TransferGroupId assigned_group(group_value);

        InMemoryTransferGroup group{
            assigned_group,
            outgoing.user_id(),
            // Persist the exact input mode from the aggregate. Never infer it
            // from currency equality (that mislabels Mode 2 cross-currency as
            // Mode 1 and can never represent Mode 3).
            static_cast<int>(transfer.mode()),
            transfer.rate()};
        store_.staged_transfer_groups.insert_or_assign(group_value, std::move(group));

        // Persist with signed-amount convention matching PostgreSQL:
        // outgoing transfer amount is negative; incoming is positive.
        // Domain TransferDomainService constructs with positive magnitudes;
        // the repository is the mapping boundary that applies storage signs.
        auto out_amount = outgoing.amount().negated();
        auto in_amount = incoming.amount();

        auto out_id = outgoing.id().is_valid()
            ? outgoing.id()
            : domain::TransactionId(store_.next_transaction_id++);
        if (outgoing.id().is_valid() && out_id.value() >= store_.next_transaction_id) {
            store_.next_transaction_id = out_id.value() + 1;
        }
        domain::Transaction out_tx(
            out_id,
            outgoing.user_id(),
            outgoing.account_id(),
            out_amount,
            domain::TransactionType::Transfer,
            outgoing.occurred_at(),
            outgoing.description(),
            outgoing.category_id(),
            assigned_group,
            outgoing.created_at(),
            outgoing.deleted_at());
        store_.staged_transactions.insert_or_assign(out_tx.id().value(), out_tx);

        auto in_id = incoming.id().is_valid()
            ? incoming.id()
            : domain::TransactionId(store_.next_transaction_id++);
        if (incoming.id().is_valid() && in_id.value() >= store_.next_transaction_id) {
            store_.next_transaction_id = in_id.value() + 1;
        }
        domain::Transaction in_tx(
            in_id,
            incoming.user_id(),
            incoming.account_id(),
            in_amount,
            domain::TransactionType::Transfer,
            incoming.occurred_at(),
            incoming.description(),
            incoming.category_id(),
            assigned_group,
            incoming.created_at(),
            incoming.deleted_at());
        store_.staged_transactions.insert_or_assign(in_tx.id().value(), in_tx);

        // Persist adjustments as independent rows in the assigned aggregate.
        // Domain instances carry an invalid placeholder group id at create
        // time, so reconstruct each row with the database-assigned id first.
        for (const auto& adj : transfer.adjustments()) {
            domain::Transaction grouped_adjustment(
                adj.id(),
                adj.user_id(),
                adj.account_id(),
                adj.amount(),
                adj.type(),
                adj.occurred_at(),
                adj.description(),
                adj.category_id(),
                assigned_group,
                adj.created_at(),
                adj.deleted_at());
            auto adj_result = save_single(tx, grouped_adjustment);
            if (!adj_result.has_value()) {
                return std::unexpected(adj_result.error());
            }
        }

        // Invalidate balance caches for both accounts.
        store_.staged_balance_cache.erase(outgoing.account_id().value());
        store_.staged_balance_cache.erase(incoming.account_id().value());
        store_.staged_deleted_balance_cache.push_back(outgoing.account_id().value());
        store_.staged_deleted_balance_cache.push_back(incoming.account_id().value());

        return domain::TransferPersistResult{assigned_group, out_id, in_id};
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Transaction>> find_by_account(
        domain::AccountId account_id,
        std::optional<std::chrono::system_clock::time_point> from = std::nullopt,
        std::optional<std::chrono::system_clock::time_point> to = std::nullopt,
        bool include_deleted = false) override {
        auto merged = merge_transactions();
        std::vector<domain::Transaction> result;
        for (const auto& [_, tx] : merged) {
            if (tx.account_id() != account_id) {
                continue;
            }
            if (!include_deleted && tx.is_deleted()) {
                continue;
            }
            if (from.has_value() && tx.occurred_at() < *from) {
                continue;
            }
            if (to.has_value() && tx.occurred_at() > *to) {
                continue;
            }
            result.push_back(tx);
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            if (a.occurred_at() != b.occurred_at()) {
                return a.occurred_at() < b.occurred_at();
            }
            return a.id().value() < b.id().value();
        });
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Transaction>> find_by_user(
        domain::UserId user_id,
        bool include_deleted = false) override {
        auto merged = merge_transactions();
        std::vector<domain::Transaction> result;
        for (const auto& [_, tx] : merged) {
            if (tx.user_id() != user_id) {
                continue;
            }
            if (!include_deleted && tx.is_deleted()) {
                continue;
            }
            result.push_back(tx);
        }
        return result;
    }

    [[nodiscard]] domain::RepositoryVoidResult soft_delete(
        domain::ITransactionContext& /*tx*/,
        domain::TransactionId id,
        domain::UserId user_id,
        std::chrono::system_clock::time_point deleted_at) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "soft_delete requires an active transaction"));
        }
        auto existing = find_by_id(id);
        if (!existing.has_value()) {
            return std::unexpected(existing.error());
        }
        if (existing->user_id() != user_id) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found for user"));
        }
        domain::Transaction updated = *existing;
        updated.mark_deleted(deleted_at);
        store_.staged_transactions.insert_or_assign(id.value(), std::move(updated));
        store_.staged_balance_cache.erase(existing->account_id().value());
        store_.staged_deleted_balance_cache.push_back(existing->account_id().value());
        return {};
    }

    [[nodiscard]] domain::RepositoryVoidResult physical_delete_by_account(
        domain::ITransactionContext& /*tx*/,
        domain::AccountId account_id) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "physical_delete_by_account requires an active transaction"));
        }
        auto merged = merge_transactions();
        for (const auto& [id, tx] : merged) {
            // Only NON-transfer rows here. Transfer legs are removed as whole
            // aggregates by physical_delete_transfers_touching_account so the
            // counterpart leg and the transfer_groups row go with them.
            if (tx.account_id() == account_id &&
                tx.type() != domain::TransactionType::Transfer) {
                store_.staged_transactions.erase(id);
                store_.staged_deleted_transactions.push_back(id);
            }
        }
        return {};
    }

    [[nodiscard]] domain::RepositoryVoidResult physical_delete_transfers_touching_account(
        domain::ITransactionContext& /*tx*/,
        domain::AccountId account_id) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "physical_delete_transfers_touching_account requires an active transaction"));
        }
        auto merged = merge_transactions();

        // 1. Collect every transfer group with any member on this account.
        //    A grouped fee Adjustment makes its third-party account part of
        //    the aggregate just as the two Transfer legs do.
        std::set<std::int64_t> group_ids;
        for (const auto& [id, tx] : merged) {
            if (tx.account_id() == account_id &&
                tx.transfer_group_id().has_value() &&
                tx.transfer_group_id()->is_valid()) {
                group_ids.insert(tx.transfer_group_id()->value());
            }
        }

        // 2. Delete every row belonging to those groups: both Transfer legs
        //    and all grouped Adjustments, then the group rows themselves.
        for (const auto& [id, tx] : merged) {
            if (tx.transfer_group_id().has_value() &&
                group_ids.count(tx.transfer_group_id()->value()) > 0) {
                store_.staged_transactions.erase(id);
                store_.staged_deleted_transactions.push_back(id);
                // Invalidate the counterpart account's balance cache too.
                store_.staged_balance_cache.erase(tx.account_id().value());
                store_.staged_deleted_balance_cache.push_back(tx.account_id().value());
            }
        }
        for (const auto gid : group_ids) {
            store_.staged_transfer_groups.erase(gid);
            store_.staged_deleted_transfer_groups.push_back(gid);
        }
        return {};
    }

private:
    [[nodiscard]] std::map<std::int64_t, domain::Transaction> merge_transactions() const {
        std::map<std::int64_t, domain::Transaction> merged = store_.transactions;
        if (store_.in_transaction) {
            for (const auto& [id, tx] : store_.staged_transactions) {
                merged.insert_or_assign(id, tx);
            }
            for (const auto id : store_.staged_deleted_transactions) {
                merged.erase(id);
            }
        }
        return merged;
    }

    [[nodiscard]] bool is_staged_deleted_tx(std::int64_t id) const {
        if (!store_.in_transaction) {
            return false;
        }
        for (const auto deleted : store_.staged_deleted_transactions) {
            if (deleted == id) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] domain::RepositoryVoidResult ensure_account_matches_transaction(
        const domain::Transaction& transaction) const {
        const domain::Account* acc = nullptr;
        if (store_.in_transaction) {
            if (auto it = store_.staged_accounts.find(transaction.account_id().value());
                it != store_.staged_accounts.end()) {
                acc = &it->second;
            }
        }
        if (acc == nullptr) {
            if (auto it = store_.accounts.find(transaction.account_id().value());
                it != store_.accounts.end()) {
                acc = &it->second;
            }
        }
        if (acc == nullptr) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Account not found: " + transaction.account_id().to_string()));
        }
        if (acc->owner() != transaction.user_id()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Account does not belong to user"));
        }
        if (!(acc->currency() == transaction.amount().currency())) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transaction currency does not match account currency: " +
                transaction.amount().currency().code() + " vs " +
                acc->currency().code()));
        }
        return {};
    }

    [[nodiscard]] domain::RepositoryVoidResult ensure_category_matches_transaction(
        const domain::Transaction& transaction) const {
        if (!transaction.category_id().has_value()) {
            return {};
        }

        if (store_.in_transaction &&
            std::find(store_.staged_deleted_categories.begin(),
                      store_.staged_deleted_categories.end(),
                      transaction.category_id()->value()) !=
                store_.staged_deleted_categories.end()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category not found for transaction user"));
        }

        const domain::Category* category = nullptr;
        if (store_.in_transaction) {
            if (auto it = store_.staged_categories.find(
                    transaction.category_id()->value());
                it != store_.staged_categories.end()) {
                category = &it->second;
            }
        }
        if (category == nullptr) {
            if (auto it = store_.categories.find(transaction.category_id()->value());
                it != store_.categories.end()) {
                category = &it->second;
            }
        }
        if (category == nullptr || category->is_deleted() ||
            category->owner() != transaction.user_id()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category not found for transaction user"));
        }
        if (!category->is_valid_for(transaction.type())) {
            return std::unexpected(domain::RepositoryError::validation(
                "Category board does not match transaction type"));
        }
        return {};
    }

    // Ownership + currency-consistency check for a transfer leg: the account
    // must exist, belong to the leg's user, and carry the leg's currency.
    [[nodiscard]] domain::RepositoryVoidResult ensure_account_matches_leg(
        const domain::Transaction& leg) const {
        return ensure_account_matches_transaction(leg);
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
