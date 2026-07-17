// Personal Finance Hub - Atomic append-only transaction correction

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/idempotency.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/application/ports/i_clock.h"
#include "pfh/application/ports/i_idempotency_repository.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"
#include "pfh/domain/repositories/i_category_repository.h"
#include "pfh/domain/repositories/i_tag_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"

namespace pfh::application {

class CorrectTransactionUseCase {
public:
    CorrectTransactionUseCase(
        domain::IAccountRepository& accounts,
        domain::ICategoryRepository& categories,
        domain::ITagRepository& tags,
        domain::ITransactionRepository& transactions,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow,
        const IClock& clock,
        IIdempotencyRepository* idempotency = nullptr)
        : accounts_(accounts), categories_(categories), tags_(tags),
          transactions_(transactions), audit_logs_(audit_logs), uow_(uow),
          clock_(clock), idempotency_(idempotency) {}

    [[nodiscard]] Result<TransactionDto> execute(
        const CorrectTransactionCommand& command,
        const IdempotencyRequest& idempotency);

private:
    domain::IAccountRepository& accounts_;
    domain::ICategoryRepository& categories_;
    domain::ITagRepository& tags_;
    domain::ITransactionRepository& transactions_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
    const IClock& clock_;
    IIdempotencyRepository* idempotency_;
};

} // namespace pfh::application
