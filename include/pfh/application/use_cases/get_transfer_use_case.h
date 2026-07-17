// Personal Finance Hub - Get Transfer Use Case

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/transfer_dto_mapper.h"
#include "pfh/domain/repositories/i_transaction_repository.h"

namespace pfh::application {

class GetTransferUseCase {
public:
    explicit GetTransferUseCase(domain::ITransactionRepository& transactions)
        : transactions_(transactions) {}

    [[nodiscard]] Result<TransferResultDto> execute(
        domain::UserId user_id,
        domain::TransferGroupId group_id) {
        if (!user_id.is_valid() || !group_id.is_valid()) {
            return err(Error::validation(
                "User and transfer group ids must be valid"));
        }
        auto snapshot = transactions_.find_transfer_by_group(group_id, user_id);
        if (!snapshot) {
            return err(from_repository(snapshot.error()));
        }
        return to_transfer_dto(*snapshot);
    }

private:
    domain::ITransactionRepository& transactions_;
};

} // namespace pfh::application
