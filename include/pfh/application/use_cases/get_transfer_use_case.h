// Personal Finance Hub - Get Transfer Use Case

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
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
        const domain::Transaction* outgoing = nullptr;
        const domain::Transaction* incoming = nullptr;
        const domain::Transaction* fee = nullptr;
        for (const auto& transaction : snapshot->transactions) {
            if (transaction.type() == domain::TransactionType::Transfer) {
                if (transaction.amount().is_negative()) {
                    if (outgoing != nullptr) {
                        return err(Error::infrastructure_failure(
                            "Persisted transfer has multiple outgoing legs"));
                    }
                    outgoing = &transaction;
                } else if (transaction.amount().is_positive()) {
                    if (incoming != nullptr) {
                        return err(Error::infrastructure_failure(
                            "Persisted transfer has multiple incoming legs"));
                    }
                    incoming = &transaction;
                }
            } else if (transaction.type() == domain::TransactionType::Adjustment &&
                       transaction.amount().is_negative()) {
                if (fee != nullptr) {
                    return err(Error::infrastructure_failure(
                        "Persisted transfer has multiple fee adjustments"));
                }
                fee = &transaction;
            }
        }
        if (outgoing == nullptr || incoming == nullptr) {
            return err(Error::infrastructure_failure(
                "Persisted transfer aggregate is incomplete"));
        }

        TransferResultDto result;
        result.transfer_group_id = group_id;
        result.outgoing_transaction_id = outgoing->id();
        result.incoming_transaction_id = incoming->id();
        result.outgoing_amount = outgoing->amount().amount().abs().to_string();
        result.incoming_amount = incoming->amount().amount().abs().to_string();
        if (snapshot->exchange_rate.has_value()) {
            result.rate = snapshot->exchange_rate->to_string();
        }
        if (fee != nullptr) {
            result.fee_amount = fee->amount().amount().abs().to_string();
        }
        return result;
    }

private:
    domain::ITransactionRepository& transactions_;
};

} // namespace pfh::application
