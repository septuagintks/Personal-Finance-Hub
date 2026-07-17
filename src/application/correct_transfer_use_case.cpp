// Personal Finance Hub - Atomic append-only transfer correction

#include "pfh/application/use_cases/correct_transfer_use_case.h"

#include "pfh/application/error_mapping.h"
#include "pfh/application/persistence_time.h"
#include "pfh/application/transfer_command_builder.h"
#include "pfh/application/transfer_dto_mapper.h"
#include "pfh/domain/events/domain_events.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace pfh::application {

Result<TransferResultDto> CorrectTransferUseCase::execute(
    const CorrectTransferCommand& command,
    const IdempotencyRequest& idempotency) {
    if (idempotency_ == nullptr) {
        return err(Error::infrastructure_failure(
            "Transfer correction idempotency is unavailable"));
    }
    if (!command.original_transfer_group_id.is_valid() ||
        command.replacement.user_id.is_valid() == false) {
        return err(Error::validation(
            "User and transfer group ids must be valid"));
    }
    if (auto valid = validate_idempotency_input(
            idempotency.key, idempotency.request_fingerprint);
        !valid) return err(valid.error());
    if (auto shape = validate_transfer_command_shape(command.replacement);
        !shape) return err(shape.error());

    std::optional<TransferResultDto> result;
    std::optional<Error> app_error;
    const auto now = normalize_persisted_time(clock_.now());
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto started = idempotency_->begin(
                tx,
                command.replacement.user_id,
                "correct_transfer",
                idempotency.key,
                idempotency.request_fingerprint,
                idempotency.created_at,
                idempotency.created_at + kIdempotencyLifetime);
            if (!started) return std::unexpected(started.error());
            if (started->replay) {
                auto restored = transfer_from_idempotency_values(
                    started->response_values);
                if (!restored) {
                    app_error = restored.error();
                    return std::unexpected(domain::RepositoryError::database(
                        "Stored transfer correction response is invalid"));
                }
                result = std::move(*restored);
                return {};
            }

            auto original = transactions_.find_transfer_by_group_for_update(
                tx,
                command.original_transfer_group_id,
                command.replacement.user_id);
            if (!original) return std::unexpected(original.error());
            if (original->deleted_at.has_value()) {
                app_error = Error::conflict(
                    "Deleted or already corrected transfers cannot be corrected");
                return std::unexpected(domain::RepositoryError::conflict(
                    "Transfer is not active"));
            }
            if (original->corrected_by_group_id.has_value()) {
                app_error = Error::conflict("Transfer is already corrected");
                return std::unexpected(domain::RepositoryError::conflict(
                    "Transfer is already corrected"));
            }

            std::set<std::int64_t> original_account_values;
            std::vector<domain::AccountId> account_ids;
            account_ids.reserve(original->transactions.size() + 3U);
            for (const auto& transaction : original->transactions) {
                original_account_values.insert(transaction.account_id().value());
                account_ids.push_back(transaction.account_id());
            }
            account_ids.push_back(command.replacement.source_account_id);
            account_ids.push_back(command.replacement.target_account_id);
            if (command.replacement.fee_source == domain::FeeSource::ThirdParty) {
                account_ids.push_back(*command.replacement.fee_account_id);
            }
            std::sort(account_ids.begin(), account_ids.end());
            account_ids.erase(
                std::unique(account_ids.begin(), account_ids.end()),
                account_ids.end());

            std::vector<domain::Account> locked_accounts;
            locked_accounts.reserve(account_ids.size());
            for (const auto account_id : account_ids) {
                auto account = accounts_.find_by_id_for_update(
                    tx, account_id, command.replacement.user_id);
                if (!account) return std::unexpected(account.error());
                if (account->is_archived() &&
                    !original_account_values.contains(account_id.value())) {
                    app_error = Error(
                        ErrorCode::ArchivedAccountOperation,
                        "Cannot move a transfer correction to an archived account");
                    return std::unexpected(domain::RepositoryError::validation(
                        "Archived replacement account"));
                }
                locked_accounts.push_back(std::move(*account));
            }
            const auto find_locked = [&](domain::AccountId id)
                -> const domain::Account* {
                const auto found = std::find_if(
                    locked_accounts.begin(), locked_accounts.end(),
                    [id](const domain::Account& account) {
                        return account.id() == id;
                    });
                return found == locked_accounts.end() ? nullptr : &*found;
            };
            const auto* source = find_locked(
                command.replacement.source_account_id);
            const auto* target = find_locked(
                command.replacement.target_account_id);
            const auto* fee_account =
                command.replacement.fee_source == domain::FeeSource::ThirdParty
                ? find_locked(*command.replacement.fee_account_id) : nullptr;
            if (source == nullptr || target == nullptr ||
                (command.replacement.fee_source == domain::FeeSource::ThirdParty &&
                 fee_account == nullptr)) {
                return std::unexpected(domain::RepositoryError::database(
                    "Locked transfer account could not be resolved"));
            }

            auto aggregate = build_transfer_aggregate(
                command.replacement, *source, *target, fee_account, now);
            if (!aggregate) {
                app_error = aggregate.error();
                return std::unexpected(domain::RepositoryError::validation(
                    "Invalid replacement transfer"));
            }
            auto persisted = transactions_.save_transfer_correction(
                tx,
                command.original_transfer_group_id,
                *aggregate,
                now);
            if (!persisted) return std::unexpected(persisted.error());
            result = to_transfer_dto(persisted->replacement, *aggregate);
            result->corrects_transfer_group_id =
                command.original_transfer_group_id;

            domain::AuditLogEntry audit;
            audit.operator_user_id = command.replacement.user_id;
            audit.action = domain::AuditAction::Update;
            audit.resource_type = "TransferCorrection";
            audit.resource_id = command.original_transfer_group_id.to_string();
            audit.before_value_json =
                "{\"transferGroupId\":" +
                command.original_transfer_group_id.to_string() + "}";
            audit.after_value_json =
                "{\"replacementTransferGroupId\":" +
                result->transfer_group_id.to_string() + "}";
            audit.metadata_json = "{}";
            audit.occurred_at = now;
            if (auto appended = audit_logs_.append(tx, audit); !appended) {
                return appended;
            }

            auto completed = idempotency_->complete(
                tx,
                command.replacement.user_id,
                "correct_transfer",
                idempotency.key,
                transfer_to_idempotency_values(*result));
            if (!completed) return completed;
            uow_.register_event(std::make_shared<domain::TransferCorrectedEvent>(
                command.replacement.user_id,
                command.original_transfer_group_id,
                result->transfer_group_id,
                now));
            return {};
        });
    if (!write) {
        if (app_error.has_value()) return err(*app_error);
        return err(from_repository(write.error()));
    }
    if (!result.has_value()) {
        return err(Error::infrastructure_failure(
            "Transfer correction result was not produced"));
    }
    return *result;
}

} // namespace pfh::application
