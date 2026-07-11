// Personal Finance Hub - DeleteTransactionUseCase
// Version: 1.0
// C++23

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/domain/events/simple_domain_event.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include <memory>

namespace pfh::application {

class DeleteTransactionUseCase {
public:
    DeleteTransactionUseCase(
        domain::ITransactionRepository& transactions,
        IUnitOfWork& uow)
        : transactions_(transactions), uow_(uow) {}

    [[nodiscard]] VoidResult execute(const DeleteTransactionCommand& cmd) {
        auto existing = transactions_.find_by_id(cmd.transaction_id);
        if (!existing) {
            return err(from_repository(existing.error()));
        }
        if (existing->user_id() != cmd.user_id) {
            // Do not leak existence across users.
            return err(Error::not_found("Transaction", cmd.transaction_id.to_string()));
        }
        if (existing->is_deleted()) {
            return err(Error::conflict("Transaction already deleted"));
        }
        if (existing->type() == domain::TransactionType::Transfer) {
            return err(Error::domain_rule_violation(
                "Transfer legs cannot be deleted independently; delete the transfer aggregate"));
        }

        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx_ctx) -> domain::RepositoryVoidResult {
                auto deleted = transactions_.soft_delete(
                    tx_ctx, cmd.transaction_id, cmd.user_id, cmd.deleted_at);
                if (!deleted) {
                    return deleted;
                }
                uow_.register_event(std::make_shared<domain::SimpleDomainEvent>(
                    "TransactionDeleted",
                    "Transaction",
                    cmd.transaction_id.to_string(),
                    "{\"account_id\":" + existing->account_id().to_string() + "}"));
                return {};
            });
        return map_repo(write);
    }

private:
    domain::ITransactionRepository& transactions_;
    IUnitOfWork& uow_;
};

} // namespace pfh::application
