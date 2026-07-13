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
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
