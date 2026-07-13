// Personal Finance Hub - DeleteTransactionUseCase
// Version: 1.0
// C++23

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/domain/events/domain_events.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace pfh::application {

class DeleteTransactionUseCase {
public:
    DeleteTransactionUseCase(
        domain::ITransactionRepository& transactions,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : transactions_(transactions), audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] VoidResult execute(const DeleteTransactionCommand& cmd) {
        if (!cmd.user_id.is_valid() || !cmd.transaction_id.is_valid()) {
            return err(Error::validation(
                "User and transaction ids must be valid"));
        }
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
                if (existing->transfer_group_id().has_value() ||
                    existing->type() == domain::TransactionType::Transfer) {
                    app_error = Error::domain_rule_violation(
                        "Transfer aggregate members cannot be deleted independently");
                    return std::unexpected(domain::RepositoryError::validation(
                        "transfer aggregate member"));
                }

                const auto deleted_at =
                    cmd.deleted_at.value_or(std::chrono::system_clock::now());
                auto deleted = transactions_.soft_delete(
                    tx_ctx, cmd.transaction_id, cmd.user_id, deleted_at);
                if (!deleted) {
                    return deleted;
                }

                domain::AuditLogEntry audit;
                audit.operator_user_id = cmd.user_id;
                audit.action = domain::AuditAction::Delete;
                audit.resource_type = "Transaction";
                audit.resource_id = cmd.transaction_id.to_string();
                audit.before_value_json =
                    "{\"accountId\":" +
                    std::to_string(existing->account_id().value()) +
                    ",\"amount\":" +
                    domain::event_detail::json_string(
                        existing->amount().amount().to_string()) +
                    ",\"currencyCode\":" +
                    domain::event_detail::json_string(
                        existing->amount().currency().code()) + "}";
                audit.after_value_json = "{\"deleted\":true}";
                audit.metadata_json = "{}";
                audit.occurred_at = deleted_at;
                if (auto appended = audit_logs_.append(tx_ctx, audit); !appended) {
                    return appended;
                }
                uow_.register_event(std::make_shared<domain::TransactionDeletedEvent>(
                    cmd.user_id,
                    cmd.transaction_id,
                    existing->account_id(),
                    deleted_at));
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
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

} // namespace pfh::application
