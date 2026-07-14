// Personal Finance Hub - In-Memory Audit Log Repository

#pragma once

#include "pfh/application/persistence/i_bootstrap_unit_of_work.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

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
        if (entry.actor_type == domain::AuditActorType::User) {
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
        store_.staged_audit_logs.push_back(entry);
        return {};
    }

private:
    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
