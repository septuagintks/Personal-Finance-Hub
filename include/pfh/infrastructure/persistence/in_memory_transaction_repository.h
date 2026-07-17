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
#include <cctype>
#include <map>
#include <set>
#include <string>
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

    [[nodiscard]] domain::RepositoryResult<domain::TransactionReadModel> find_detail(
        domain::TransactionId id,
        domain::UserId user_id,
        bool include_deleted = true) override {
        auto transaction = find_by_id(id);
        if (!transaction || transaction->user_id() != user_id ||
            (!include_deleted && transaction->is_deleted())) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found for user"));
        }
        return make_read_model(*transaction);
    }

    [[nodiscard]] domain::RepositoryResult<domain::TransactionPageResult> find_page(
        const domain::TransactionPageQuery& query) override {
        if (!query.user_id.is_valid() || query.limit == 0 || query.limit > 200) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transaction page query is invalid"));
        }

        const auto lowered_keyword = lowercase(query.keyword);
        std::vector<domain::Transaction> matched;
        for (const auto& [_, transaction] : merge_transactions()) {
            if (transaction.user_id() != query.user_id || transaction.is_deleted()) {
                continue;
            }
            if (query.account_id.has_value() &&
                transaction.account_id() != *query.account_id) {
                continue;
            }
            if (query.type.has_value() && transaction.type() != *query.type) {
                continue;
            }
            if (query.category_id.has_value() &&
                transaction.category_id() != query.category_id) {
                continue;
            }
            if (query.tag_id.has_value() &&
                !transaction_has_tag(transaction.id(), *query.tag_id)) {
                continue;
            }
            if (query.occurred_from.has_value() &&
                transaction.occurred_at() < *query.occurred_from) {
                continue;
            }
            if (query.occurred_to.has_value() &&
                transaction.occurred_at() >= *query.occurred_to) {
                continue;
            }
            if (!lowered_keyword.empty() &&
                lowercase(transaction.description()).find(lowered_keyword) ==
                    std::string::npos) {
                continue;
            }
            if (query.before.has_value()) {
                const auto& cursor = *query.before;
                if (transaction.occurred_at() > cursor.occurred_at ||
                    (transaction.occurred_at() == cursor.occurred_at &&
                     transaction.id() >= cursor.id)) {
                    continue;
                }
            }
            matched.push_back(transaction);
        }
        std::sort(matched.begin(), matched.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.occurred_at() != rhs.occurred_at()) {
                return lhs.occurred_at() > rhs.occurred_at();
            }
            return lhs.id() > rhs.id();
        });

        domain::TransactionPageResult result;
        result.has_more = matched.size() > query.limit;
        if (result.has_more) {
            matched.erase(
                matched.begin() + static_cast<std::ptrdiff_t>(query.limit),
                matched.end());
        }
        result.items.reserve(matched.size());
        for (const auto& transaction : matched) {
            result.items.push_back(make_read_model(transaction));
        }
        return result;
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

        if (transaction.id().is_valid() || transaction.is_deleted()) {
            return std::unexpected(domain::RepositoryError::validation(
                "save_single only accepts new active transactions"));
        }
        if (transaction.transfer_group_id().has_value()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Grouped transactions must be written via save_transfer"));
        }
        if (transaction.type() != domain::TransactionType::Income &&
            transaction.type() != domain::TransactionType::Expense &&
            transaction.type() != domain::TransactionType::Transfer &&
            transaction.type() != domain::TransactionType::Adjustment) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transaction type is invalid"));
        }
        // Transfer legs may ONLY be written via save_transfer (as a validated
        // aggregate). Reject standalone Transfer rows so no orphan leg can be
        // created that bypasses the aggregate consistency rules.
        if (transaction.type() == domain::TransactionType::Transfer) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer transactions must be written via save_transfer, not save_single"));
        }
        if (auto valid = validate_amount_for_storage(transaction.amount());
            !valid.has_value()) {
            return std::unexpected(valid.error());
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

        // Normalize the public positive-magnitude convention at the mapping
        // boundary. Persisted entities are never accepted by this create API.
        domain::Money storage_amount = transaction.amount();
        if (transaction.type() == domain::TransactionType::Expense) {
            if (!storage_amount.is_positive()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Expense amount must be a positive magnitude"));
            }
            storage_amount = storage_amount.negated();
        } else if (transaction.type() == domain::TransactionType::Income &&
                   !storage_amount.is_positive()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Income amount must be a positive magnitude"));
        }

        const auto id_value = store_.next_transaction_id++;
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
        domain::ITransactionContext& /*tx*/,
        const domain::TransferAggregate& transfer) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "save_transfer requires an active transaction"));
        }

        const auto& outgoing = transfer.outgoing();
        const auto& incoming = transfer.incoming();

        if (outgoing.id().is_valid() || incoming.id().is_valid() ||
            outgoing.is_deleted() || incoming.is_deleted()) {
            return std::unexpected(domain::RepositoryError::validation(
                "save_transfer only accepts new active transactions"));
        }
        if ((outgoing.transfer_group_id().has_value() &&
             outgoing.transfer_group_id()->is_valid()) ||
            (incoming.transfer_group_id().has_value() &&
             incoming.transfer_group_id()->is_valid())) {
            return std::unexpected(domain::RepositoryError::validation(
                "New transfer must not carry a persisted group identifier"));
        }
        if (outgoing.type() != domain::TransactionType::Transfer ||
            incoming.type() != domain::TransactionType::Transfer ||
            outgoing.account_id() == incoming.account_id() ||
            outgoing.user_id() != incoming.user_id() ||
            !outgoing.amount().is_positive() ||
            !incoming.amount().is_positive()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer aggregate invariants are invalid"));
        }
        if (auto valid = validate_amount_for_storage(outgoing.amount());
            !valid.has_value()) {
            return std::unexpected(valid.error());
        }
        if (auto valid = validate_amount_for_storage(incoming.amount());
            !valid.has_value()) {
            return std::unexpected(valid.error());
        }
        if (transfer.rate().has_value() &&
            !transfer.rate()->rate().fits_numeric_20_10()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer rate does not fit NUMERIC(20,10)"));
        }
        for (const auto& adjustment : transfer.adjustments()) {
            if (adjustment.user_id() != outgoing.user_id() ||
                adjustment.type() != domain::TransactionType::Adjustment ||
                adjustment.id().is_valid() || adjustment.is_deleted() ||
                adjustment.occurred_at() != outgoing.occurred_at() ||
                adjustment.transfer_group_id() != outgoing.transfer_group_id()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Transfer adjustment invariants are invalid"));
            }
            if (auto valid = validate_amount_for_storage(adjustment.amount());
                !valid.has_value()) {
                return std::unexpected(valid.error());
            }
            if (auto check = ensure_account_matches_transaction(adjustment);
                !check.has_value()) {
                return std::unexpected(check.error());
            }
            if (auto check = ensure_category_matches_transaction(adjustment);
                !check.has_value()) {
                return std::unexpected(check.error());
            }
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
        if (auto check = ensure_category_matches_transaction(outgoing);
            !check.has_value()) {
            return std::unexpected(check.error());
        }
        if (auto check = ensure_category_matches_transaction(incoming);
            !check.has_value()) {
            return std::unexpected(check.error());
        }

        // A create always receives a fresh persistence identifier. Domain may
        // carry an invalid placeholder, but a valid id is rejected above.
        const auto group_value = store_.next_transfer_group_id++;
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

        const domain::TransactionId out_id(store_.next_transaction_id++);
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

        const domain::TransactionId in_id(store_.next_transaction_id++);
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

        // Persist grouped adjustments through this aggregate-only insertion
        // path. Calling public save_single would violate its deliberate rule
        // that no grouped transaction can be inserted independently.
        for (const auto& adj : transfer.adjustments()) {
            domain::Transaction grouped_adjustment(
                domain::TransactionId(store_.next_transaction_id++),
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
            store_.staged_transactions.insert_or_assign(
                grouped_adjustment.id().value(), grouped_adjustment);
            store_.staged_balance_cache.erase(adj.account_id().value());
            store_.staged_deleted_balance_cache.push_back(
                adj.account_id().value());
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
            if (to.has_value() && tx.occurred_at() >= *to) {
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
        return find_by_user_in_range(
            user_id, std::nullopt, std::nullopt, include_deleted);
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Transaction>>
    find_by_user_in_range(
        domain::UserId user_id,
        std::optional<std::chrono::system_clock::time_point> from,
        std::optional<std::chrono::system_clock::time_point> to,
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
            if (from.has_value() && tx.occurred_at() < *from) {
                continue;
            }
            if (to.has_value() && tx.occurred_at() >= *to) {
                continue;
            }
            result.push_back(tx);
        }
        std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.occurred_at() != rhs.occurred_at()) {
                return lhs.occurred_at() < rhs.occurred_at();
            }
            return lhs.id() < rhs.id();
        });
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<domain::TransferSnapshot>
    find_transfer_by_group(
        domain::TransferGroupId group_id,
        domain::UserId user_id) override {
        const InMemoryTransferGroup* group = nullptr;
        if (store_.in_transaction) {
            if (const auto found = store_.staged_transfer_groups.find(group_id.value());
                found != store_.staged_transfer_groups.end()) {
                group = &found->second;
            }
        }
        if (group == nullptr) {
            if (const auto found = store_.transfer_groups.find(group_id.value());
                found != store_.transfer_groups.end()) {
                group = &found->second;
            }
        }
        if (group == nullptr || group->user_id != user_id) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transfer not found for user"));
        }
        domain::TransferSnapshot snapshot;
        snapshot.group_id = group_id;
        snapshot.user_id = user_id;
        snapshot.transfer_mode = group->transfer_mode;
        if (group->rate.has_value()) {
            snapshot.exchange_rate = group->rate->rate();
        }
        for (const auto& [_, transaction] : merge_transactions()) {
            if (transaction.transfer_group_id() ==
                    std::optional<domain::TransferGroupId>(group_id) &&
                !transaction.is_deleted()) {
                snapshot.transactions.push_back(transaction);
            }
        }
        return snapshot;
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

    [[nodiscard]] domain::RepositoryResult<domain::TransactionCorrectionPersistResult>
    save_correction(
        domain::ITransactionContext& tx,
        domain::TransactionId original_id,
        const domain::Transaction& replacement,
        std::chrono::system_clock::time_point corrected_at) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "save_correction requires an active transaction"));
        }
        auto original = find_by_id(original_id);
        if (!original || original->user_id() != replacement.user_id()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found for user"));
        }
        if (original->is_deleted()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Transaction is already deleted"));
        }
        if (original->type() == domain::TransactionType::Transfer ||
            original->transfer_group_id().has_value()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer aggregate members cannot be corrected independently"));
        }
        if (merged_corrections().contains(original_id.value())) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Transaction is already corrected"));
        }

        auto saved = save_single(tx, replacement);
        if (!saved) return std::unexpected(saved.error());
        auto deleted = soft_delete(
            tx, original_id, replacement.user_id(), corrected_at);
        if (!deleted) return std::unexpected(deleted.error());

        store_.staged_transaction_corrections.insert_or_assign(
            original_id.value(),
            InMemoryTransactionCorrection{
                replacement.user_id(), original_id, saved->id(), corrected_at});
        return domain::TransactionCorrectionPersistResult{*saved};
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
                stage_correction_deletions_for(id);
                store_.staged_transactions.erase(id);
                store_.staged_deleted_transactions.push_back(id);
                store_.staged_transaction_tag_relations.insert_or_assign(
                    id, std::set<std::int64_t>{});
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
                stage_correction_deletions_for(id);
                store_.staged_transactions.erase(id);
                store_.staged_deleted_transactions.push_back(id);
                store_.staged_transaction_tag_relations.insert_or_assign(
                    id, std::set<std::int64_t>{});
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
    [[nodiscard]] static std::string lowercase(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](char raw) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(raw)));
        });
        return value;
    }

    [[nodiscard]] std::map<std::int64_t, domain::Category>
    merged_categories() const {
        auto merged = store_.categories;
        if (store_.in_transaction) {
            for (const auto& [id, value] : store_.staged_categories) {
                merged.insert_or_assign(id, value);
            }
            for (const auto id : store_.staged_deleted_categories) {
                merged.erase(id);
            }
        }
        return merged;
    }

    [[nodiscard]] std::map<std::int64_t, domain::Tag> merged_tags() const {
        auto merged = store_.tags;
        if (store_.in_transaction) {
            for (const auto& [id, value] : store_.staged_tags) {
                merged.insert_or_assign(id, value);
            }
            for (const auto id : store_.staged_deleted_tags) {
                merged.erase(id);
            }
        }
        return merged;
    }

    [[nodiscard]] std::set<std::int64_t> tag_relations(
        domain::TransactionId transaction_id) const {
        if (store_.in_transaction) {
            if (const auto staged = store_.staged_transaction_tag_relations.find(
                    transaction_id.value());
                staged != store_.staged_transaction_tag_relations.end()) {
                return staged->second;
            }
        }
        if (const auto found = store_.transaction_tag_relations.find(
                transaction_id.value());
            found != store_.transaction_tag_relations.end()) {
            return found->second;
        }
        return {};
    }

    [[nodiscard]] bool transaction_has_tag(
        domain::TransactionId transaction_id,
        domain::TagId tag_id) const {
        return tag_relations(transaction_id).contains(tag_id.value());
    }

    [[nodiscard]] std::map<std::int64_t, InMemoryTransactionCorrection>
    merged_corrections() const {
        auto merged = store_.transaction_corrections;
        if (store_.in_transaction) {
            for (const auto& [id, value] : store_.staged_transaction_corrections) {
                merged.insert_or_assign(id, value);
            }
            for (const auto id : store_.staged_deleted_transaction_corrections) {
                merged.erase(id);
            }
        }
        return merged;
    }

    [[nodiscard]] domain::TransactionReadModel make_read_model(
        const domain::Transaction& transaction) const {
        domain::TransactionReadModel result{
            transaction, std::nullopt, false, {}, std::nullopt, std::nullopt};
        if (transaction.category_id().has_value()) {
            const auto categories = merged_categories();
            if (const auto found = categories.find(
                    transaction.category_id()->value());
                found != categories.end()) {
                result.category_name = found->second.name();
                result.category_deleted = found->second.is_deleted();
            }
        }
        const auto tags = merged_tags();
        for (const auto tag_id : tag_relations(transaction.id())) {
            if (const auto found = tags.find(tag_id); found != tags.end()) {
                result.tags.push_back(found->second);
            }
        }
        std::sort(result.tags.begin(), result.tags.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.name() != rhs.name()) return lhs.name() < rhs.name();
            return lhs.id() < rhs.id();
        });
        for (const auto& [_, correction] : merged_corrections()) {
            if (correction.original_transaction_id == transaction.id()) {
                result.corrected_by_transaction_id =
                    correction.replacement_transaction_id;
            }
            if (correction.replacement_transaction_id == transaction.id()) {
                result.corrects_transaction_id =
                    correction.original_transaction_id;
            }
        }
        return result;
    }

    void stage_correction_deletions_for(std::int64_t transaction_id) {
        for (const auto& [original_id, correction] : merged_corrections()) {
            if (original_id == transaction_id ||
                correction.replacement_transaction_id.value() == transaction_id) {
                store_.staged_transaction_corrections.erase(original_id);
                store_.staged_deleted_transaction_corrections.push_back(original_id);
            }
        }
    }

    [[nodiscard]] static domain::RepositoryVoidResult validate_amount_for_storage(
        const domain::Money& amount) {
        if (amount.is_zero()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transaction amount must be non-zero"));
        }
        if (!amount.amount().fits_numeric_20_8()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transaction amount does not fit NUMERIC(20,8)"));
        }
        return {};
    }

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
