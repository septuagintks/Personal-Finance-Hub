// Personal Finance Hub - Transfer aggregate query use cases

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/domain/repositories/i_transaction_repository.h"

namespace pfh::application {

class ListTransfersUseCase {
public:
    explicit ListTransfersUseCase(domain::ITransactionRepository& transactions)
        : transactions_(transactions) {}

    [[nodiscard]] Result<CursorPage<TransferResultDto>> execute(
        const TransferListQuery& query);

private:
    domain::ITransactionRepository& transactions_;
};

} // namespace pfh::application
