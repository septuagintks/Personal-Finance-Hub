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
#include <chrono>
#include <memory>
#include <optional>

namespace pfh::application {

class DeleteTransactionUseCase {
public:
    DeleteTransactionUseCase(
        domain::ITransactionRepository& transactions,
        IUnitOfWork& uow)
        : transactions_(transactions), uow_(uow) {}

    [[nodiscard]] VoidResult execute(const DeleteTransactionCommand& cmd) {
        // Load, validate and delete inside ONE transaction with the row locked,
        // so the not-already-deleted / not-a-transfer checks cannot race a
        // concurrent delete between the read and the write.
        std::optional<Error> app_error;
        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx_ctx) -> domain::RepositoryVoidResult {
                auto existing =
                    transactions_.find_by_id_for_update(tx_ctx, cmd.transaction_id);
                if (!existing) {
                    return std::unexpected(existing.error());
                }
                if (existing->user_id() != cmd.user_id) {
                    // Do not leak existence across users.
                    app_error = Error::not_found("Transaction", cmd.transaction_id.to_string());
                    return std::unexpected(domain::RepositoryError::not_found("not found"));
                }
                if (existing->is_deleted()) {
                    app_error = Error::conflict("Transaction already deleted");
                    return std::unexpected(domain::RepositoryError::conflict("already deleted"));
                }
                if (existing->type() == domain::TransactionType::Transfer) {
                    app_error = Error::domain_rule_violation(
                        "Transfer legs cannot be deleted independently; delete the transfer aggregate");
                    return std::unexpected(domain::RepositoryError::validation("transfer leg"));
                }

                const auto deleted_at =
                    cmd.deleted_at.value_or(std::chrono::system_clock::now());
                auto deleted = transactions_.soft_delete(
                    tx_ctx, cmd.transaction_id, cmd.user_id, deleted_at);
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
        if (!write) {
            if (app_error.has_value()) {
                return err(*app_error);
            }
            return err(from_repository(write.error()));
        }
        return ok();
    }

private:
    domain::ITransactionRepository& transactions_;
    IUnitOfWork& uow_;
};

} // namespace pfh::application
