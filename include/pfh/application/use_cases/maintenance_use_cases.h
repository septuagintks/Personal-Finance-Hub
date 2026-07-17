// Personal Finance Hub - Authenticated User Maintenance Use Cases

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/application/ports/i_clock.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"

namespace pfh::application {

class ListUserAuditLogsUseCase {
public:
    ListUserAuditLogsUseCase(
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] Result<UserAuditLogPageDto> execute(
        const UserAuditLogQueryDto& query);

private:
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class RebuildBalanceCacheUseCase {
public:
    RebuildBalanceCacheUseCase(
        domain::IAccountRepository& accounts,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow,
        const IClock& clock)
        : accounts_(accounts), audit_logs_(audit_logs), uow_(uow), clock_(clock) {}

    [[nodiscard]] Result<BalanceCacheRebuildDto> execute(
        const RebuildBalanceCacheCommand& command);

private:
    domain::IAccountRepository& accounts_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
    const IClock& clock_;
};

} // namespace pfh::application
