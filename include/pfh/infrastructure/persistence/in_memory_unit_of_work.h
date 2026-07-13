// Personal Finance Hub - In-Memory Unit of Work
// Version: 1.0
// C++23
//
// Models the same ACID boundary as DrogonUnitOfWork:
// - One transaction context for all repository writes in a closure
// - Business writes + outbox writes commit together
// - Failure rolls back both; no dirty residual state
// - Events are never published before commit (only staged into outbox)

#pragma once

#include "pfh/application/persistence/i_bootstrap_unit_of_work.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include "pfh/infrastructure/persistence/in_memory_transaction_context.h"
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

class InMemoryUnitOfWork final : public application::IBootstrapUnitOfWork {
public:
    explicit InMemoryUnitOfWork(
        InMemoryStore& store,
        std::optional<domain::UserId> tenant_user_id = std::nullopt)
        : store_(store), tenant_user_id_(tenant_user_id) {}

    void register_event(std::shared_ptr<domain::IDomainEvent> event) override {
        pending_events_.push_back(std::move(event));
    }

    [[nodiscard]] domain::RepositoryVoidResult execute_in_transaction(
        std::function<domain::RepositoryVoidResult(domain::ITransactionContext& tx)> action) override {
        return run_transaction([&](InMemoryTransactionContext& tx) {
            return action(tx);
        });
    }

    [[nodiscard]] domain::RepositoryVoidResult execute_bootstrap_transaction(
        std::function<domain::RepositoryVoidResult(
            application::ITenantBootstrapTransaction& tx)> action) override {
        if (tenant_user_id_.has_value()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Bootstrap transaction must start without a tenant"));
        }
        return run_transaction([&](InMemoryTransactionContext& tx) {
            return action(tx);
        });
    }

private:
    template <typename Action>
    [[nodiscard]] domain::RepositoryVoidResult run_transaction(Action&& action) {
        if (store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "Nested transactions are not supported"));
        }

        // Begin: clear staging areas and mark active transaction.
        pending_events_.clear();
        store_.in_transaction = true;
        store_.staged_users.clear();
        store_.staged_preferences.clear();
        store_.staged_accounts.clear();
        store_.staged_transactions.clear();
        store_.staged_categories.clear();
        store_.staged_transfer_groups.clear();
        store_.staged_balance_cache.clear();
        store_.staged_exchange_rates.clear();
        store_.staged_outbox.clear();
        store_.staged_refresh_tokens.clear();
        store_.staged_revoked_access_tokens.clear();
        store_.staged_revoked_sessions.clear();
        store_.staged_audit_logs.clear();
        store_.staged_deleted_accounts.clear();
        store_.staged_deleted_transactions.clear();
        store_.staged_deleted_balance_cache.clear();
        store_.staged_deleted_transfer_groups.clear();
        store_.staged_deleted_categories.clear();

        InMemoryTransactionContext tx(
            store_.next_tx_context_id++, tenant_user_id_);
        auto result = std::forward<Action>(action)(tx);

        if (!result.has_value()) {
            // Rollback: discard staging, keep committed store intact.
            store_.in_transaction = false;
            store_.staged_users.clear();
            store_.staged_preferences.clear();
            store_.staged_accounts.clear();
            store_.staged_transactions.clear();
            store_.staged_categories.clear();
            store_.staged_transfer_groups.clear();
            store_.staged_balance_cache.clear();
            store_.staged_exchange_rates.clear();
            store_.staged_outbox.clear();
            store_.staged_refresh_tokens.clear();
            store_.staged_revoked_access_tokens.clear();
            store_.staged_revoked_sessions.clear();
            store_.staged_audit_logs.clear();
            store_.staged_deleted_accounts.clear();
            store_.staged_deleted_transactions.clear();
            store_.staged_deleted_balance_cache.clear();
            store_.staged_deleted_transfer_groups.clear();
            store_.staged_deleted_categories.clear();
            pending_events_.clear();
            return result;
        }

        // Stage outbox rows in the same transaction before commit.
        for (const auto& event : pending_events_) {
            OutboxRecord rec;
            rec.id = "outbox-" + std::to_string(store_.next_outbox_id++);
            rec.event_name = event->event_name();
            rec.aggregate_type = event->aggregate_type();
            rec.aggregate_id = event->aggregate_id();
            rec.payload_json = event->payload_json();
            rec.status = "pending";
            rec.retry_count = 0;
            rec.occurred_at = event->occurred_at();
            store_.staged_outbox.push_back(std::move(rec));
        }

        // Commit: merge staged data into permanent store.
        for (auto& [id, rec] : store_.staged_users) {
            store_.users.insert_or_assign(id, std::move(rec));
        }
        for (auto& [id, pref] : store_.staged_preferences) {
            store_.preferences.insert_or_assign(id, std::move(pref));
        }
        for (auto& [id, acc] : store_.staged_accounts) {
            store_.accounts.insert_or_assign(id, std::move(acc));
        }
        for (auto& [id, tx_rec] : store_.staged_transactions) {
            store_.transactions.insert_or_assign(id, std::move(tx_rec));
        }
        for (auto& [id, cat] : store_.staged_categories) {
            store_.categories.insert_or_assign(id, std::move(cat));
        }
        for (auto& [id, tg] : store_.staged_transfer_groups) {
            store_.transfer_groups.insert_or_assign(id, std::move(tg));
        }
        for (auto& [id, cache] : store_.staged_balance_cache) {
            store_.balance_cache.insert_or_assign(id, std::move(cache));
        }
        for (auto& [id, rate] : store_.staged_exchange_rates) {
            store_.exchange_rates.insert_or_assign(id, std::move(rate));
        }
        for (auto& outbox : store_.staged_outbox) {
            store_.outbox.push_back(std::move(outbox));
        }
        for (auto& [hash, token] : store_.staged_refresh_tokens) {
            store_.refresh_tokens.insert_or_assign(hash, std::move(token));
        }
        for (auto& [key, token] : store_.staged_revoked_access_tokens) {
            store_.revoked_access_tokens.insert_or_assign(key, std::move(token));
        }
        for (auto& [session_id, session] : store_.staged_revoked_sessions) {
            store_.revoked_sessions.insert_or_assign(session_id, std::move(session));
        }
        for (auto& audit : store_.staged_audit_logs) {
            store_.audit_logs.push_back(std::move(audit));
        }
        for (const auto id : store_.staged_deleted_accounts) {
            store_.accounts.erase(id);
        }
        for (const auto id : store_.staged_deleted_transactions) {
            store_.transactions.erase(id);
        }
        for (const auto id : store_.staged_deleted_balance_cache) {
            store_.balance_cache.erase(id);
        }
        for (const auto id : store_.staged_deleted_transfer_groups) {
            store_.transfer_groups.erase(id);
        }
        for (const auto id : store_.staged_deleted_categories) {
            store_.categories.erase(id);
        }

        store_.in_transaction = false;
        store_.staged_users.clear();
        store_.staged_preferences.clear();
        store_.staged_accounts.clear();
        store_.staged_transactions.clear();
        store_.staged_categories.clear();
        store_.staged_transfer_groups.clear();
        store_.staged_balance_cache.clear();
        store_.staged_exchange_rates.clear();
        store_.staged_outbox.clear();
        store_.staged_refresh_tokens.clear();
        store_.staged_revoked_access_tokens.clear();
        store_.staged_revoked_sessions.clear();
        store_.staged_audit_logs.clear();
        store_.staged_deleted_accounts.clear();
        store_.staged_deleted_transactions.clear();
        store_.staged_deleted_balance_cache.clear();
        store_.staged_deleted_transfer_groups.clear();
        store_.staged_deleted_categories.clear();
        pending_events_.clear();
        return {};
    }

    InMemoryStore& store_;
    std::optional<domain::UserId> tenant_user_id_;
    std::vector<std::shared_ptr<domain::IDomainEvent>> pending_events_;
};

} // namespace pfh::infrastructure
