// Personal Finance Hub - In-Memory Audit Log Repository

#pragma once

#include "pfh/application/persistence/i_bootstrap_unit_of_work.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <algorithm>

namespace pfh::infrastructure {

class InMemoryAuditLogRepository final : public domain::IAuditLogRepository {
public:
    explicit InMemoryAuditLogRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryVoidResult append(
        domain::ITransactionContext& tx,
        const domain::AuditLogEntry& entry) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "Audit append requires an active transaction"));
        }
        const auto* tenant_tx =
            dynamic_cast<const application::ITenantBootstrapTransaction*>(&tx);
        if (tenant_tx == nullptr) {
            return std::unexpected(domain::RepositoryError::validation(
                "Audit transaction tenant mismatch"));
        }
        if (entry.actor_type == domain::AuditActorType::User ||
            entry.actor_type == domain::AuditActorType::Operator) {
            if (!entry.operator_user_id.has_value() ||
                !entry.operator_user_id->is_valid() ||
                tenant_tx->tenant_user_id() != entry.operator_user_id) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Audit transaction tenant mismatch"));
            }
        } else if (entry.operator_user_id.has_value() ||
                   tenant_tx->tenant_user_id().has_value()) {
            return std::unexpected(domain::RepositoryError::validation(
                "System audit requires an unscoped transaction"));
        }
        if (entry.resource_type.empty() || entry.resource_id.empty()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Audit resource is required"));
        }
        auto persisted = entry;
        persisted.id = store_.next_audit_log_id++;
        store_.staged_audit_logs.push_back(std::move(persisted));
        return {};
    }

    [[nodiscard]] domain::RepositoryResult<domain::UserAuditLogPage>
    find_user_entries(
        domain::ITransactionContext& tx,
        const domain::UserAuditLogQuery& query) override {
        if (!store_.in_transaction || !query.user_id.is_valid() ||
            query.limit == 0 || query.limit > 100 ||
            (query.from.has_value() && query.to.has_value() &&
             *query.from > *query.to)) {
            return std::unexpected(domain::RepositoryError::validation(
                "Audit query is invalid"));
        }
        const auto* tenant_tx =
            dynamic_cast<const application::ITenantBootstrapTransaction*>(&tx);
        if (tenant_tx == nullptr ||
            tenant_tx->tenant_user_id() != query.user_id) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Audit entries not found"));
        }

        std::vector<domain::AuditLogEntry> matches;
        for (const auto& entry : store_.audit_logs) {
            const bool owned_user_fact =
                entry.actor_type == domain::AuditActorType::User &&
                entry.operator_user_id == query.user_id;
            if (!owned_user_fact ||
                (query.action.has_value() && entry.action != *query.action) ||
                (query.resource_type.has_value() &&
                 entry.resource_type != *query.resource_type) ||
                (query.from.has_value() && entry.occurred_at < *query.from) ||
                (query.to.has_value() && entry.occurred_at >= *query.to) ||
                (query.before_id.has_value() && entry.id >= *query.before_id)) {
                continue;
            }
            matches.push_back(entry);
        }
        std::ranges::sort(matches, [](const auto& left, const auto& right) {
            return left.id > right.id;
        });

        domain::UserAuditLogPage page;
        if (matches.size() > query.limit) {
            matches.resize(query.limit);
            page.next_before_id = matches.back().id;
        }
        page.entries = std::move(matches);
        return page;
    }

private:
    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
