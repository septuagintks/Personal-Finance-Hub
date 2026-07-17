// Personal Finance Hub - Aggregate-level transfer soft deletion

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/application/ports/i_clock.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"

namespace pfh::application {

class DeleteTransferUseCase {
public:
    DeleteTransferUseCase(
        domain::IAccountRepository& accounts,
        domain::ITransactionRepository& transactions,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow,
        const IClock& clock)
        : accounts_(accounts), transactions_(transactions),
          audit_logs_(audit_logs), uow_(uow), clock_(clock) {}

    [[nodiscard]] VoidResult execute(const DeleteTransferCommand& command);

private:
    domain::IAccountRepository& accounts_;
    domain::ITransactionRepository& transactions_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
    const IClock& clock_;
};

} // namespace pfh::application
