// Personal Finance Hub - Audit Log Repository Interface

#pragma once

#include "pfh/domain/audit_log.h"
#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"

namespace pfh::domain {

class IAuditLogRepository {
public:
    virtual ~IAuditLogRepository() = default;

    [[nodiscard]] virtual RepositoryVoidResult append(
        ITransactionContext& tx,
        const AuditLogEntry& entry) = 0;

    [[nodiscard]] virtual RepositoryResult<UserAuditLogPage> find_user_entries(
        ITransactionContext& tx,
        const UserAuditLogQuery& query) = 0;
};

} // namespace pfh::domain
