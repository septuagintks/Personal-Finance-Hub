// Personal Finance Hub - Ledger transaction queries

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/domain/repositories/i_transaction_repository.h"

namespace pfh::application {

class ListTransactionsUseCase {
public:
    explicit ListTransactionsUseCase(domain::ITransactionRepository& transactions)
        : transactions_(transactions) {}

    [[nodiscard]] Result<CursorPage<TransactionDto>> execute(
        const TransactionListQuery& query);

private:
    domain::ITransactionRepository& transactions_;
};

class GetTransactionUseCase {
public:
    explicit GetTransactionUseCase(domain::ITransactionRepository& transactions)
        : transactions_(transactions) {}

    [[nodiscard]] Result<TransactionDto> execute(
        domain::UserId user_id,
        domain::TransactionId transaction_id);

private:
    domain::ITransactionRepository& transactions_;
};

} // namespace pfh::application
