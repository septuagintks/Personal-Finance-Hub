// Personal Finance Hub - Aggregate-level transfer soft deletion

#include "pfh/application/use_cases/delete_transfer_use_case.h"

#include "pfh/application/error_mapping.h"
#include "pfh/application/persistence_time.h"
#include "pfh/domain/events/domain_events.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace pfh::application {

VoidResult DeleteTransferUseCase::execute(
    const DeleteTransferCommand& command) {
    if (!command.user_id.is_valid() ||
        !command.transfer_group_id.is_valid()) {
        return err(Error::validation(
            "User and transfer group ids must be valid"));
    }
    const auto deleted_at = normalize_persisted_time(
        command.deleted_at.value_or(clock_.now()));
    std::optional<Error> app_error;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto snapshot = transactions_.find_transfer_by_group_for_update(
                tx, command.transfer_group_id, command.user_id);
            if (!snapshot) return std::unexpected(snapshot.error());
            if (snapshot->deleted_at.has_value()) {
                app_error = Error::conflict("Transfer already deleted");
                return std::unexpected(domain::RepositoryError::conflict(
                    "Transfer already deleted"));
            }

            std::vector<domain::AccountId> account_ids;
            account_ids.reserve(snapshot->transactions.size());
            for (const auto& transaction : snapshot->transactions) {
                if (transaction.is_deleted()) {
                    return std::unexpected(domain::RepositoryError::database(
                        "Transfer aggregate is partially deleted"));
                }
                account_ids.push_back(transaction.account_id());
            }
            std::sort(account_ids.begin(), account_ids.end());
            account_ids.erase(
                std::unique(account_ids.begin(), account_ids.end()),
                account_ids.end());
            for (const auto account_id : account_ids) {
                auto locked = accounts_.find_by_id_for_update(
                    tx, account_id, command.user_id);
                if (!locked) return std::unexpected(locked.error());
            }

            auto deleted = transactions_.soft_delete_transfer(
                tx, command.transfer_group_id, command.user_id, deleted_at);
            if (!deleted) return deleted;

            domain::AuditLogEntry audit;
            audit.operator_user_id = command.user_id;
            audit.action = domain::AuditAction::Delete;
            audit.resource_type = "TransferGroup";
            audit.resource_id = command.transfer_group_id.to_string();
            audit.before_value_json =
                "{\"memberCount\":" +
                std::to_string(snapshot->transactions.size()) + "}";
            audit.after_value_json = "{\"deleted\":true}";
            audit.metadata_json = "{}";
            audit.occurred_at = deleted_at;
            if (auto appended = audit_logs_.append(tx, audit); !appended) {
                return appended;
            }
            uow_.register_event(std::make_shared<domain::TransferDeletedEvent>(
                command.user_id, command.transfer_group_id, deleted_at));
            return {};
        });
    if (!write) {
        if (app_error.has_value()) return err(*app_error);
        return err(from_repository(write.error()));
    }
    return ok();
}

} // namespace pfh::application
