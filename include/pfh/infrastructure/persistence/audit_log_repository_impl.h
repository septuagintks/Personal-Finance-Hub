// Personal Finance Hub - PostgreSQL Audit Log Repository

#pragma once

#include "pfh/domain/repositories/i_audit_log_repository.h"

#ifdef PFH_HAS_POSTGRESQL

namespace pfh::infrastructure {

class AuditLogRepositoryImpl final : public domain::IAuditLogRepository {
public:
    [[nodiscard]] domain::RepositoryVoidResult append(
        domain::ITransactionContext& tx,
        const domain::AuditLogEntry& entry) override;

    [[nodiscard]] domain::RepositoryResult<domain::UserAuditLogPage>
    find_user_entries(
        domain::ITransactionContext& tx,
        const domain::UserAuditLogQuery& query) override;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
