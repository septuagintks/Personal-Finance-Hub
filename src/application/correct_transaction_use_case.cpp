// Personal Finance Hub - Atomic append-only transaction correction

#include "pfh/application/use_cases/correct_transaction_use_case.h"

#include "pfh/application/error_mapping.h"
#include "pfh/application/input_constraints.h"
#include "pfh/application/persistence_time.h"
#include "pfh/application/transaction_dto_mapper.h"
#include "pfh/domain/events/domain_events.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace pfh::application {

Result<TransactionDto> CorrectTransactionUseCase::execute(
    const CorrectTransactionCommand& command,
    const IdempotencyRequest& idempotency) {
    if (idempotency_ == nullptr) {
        return err(Error::infrastructure_failure(
            "Transaction correction idempotency is unavailable"));
    }
    if (auto valid = validate_idempotency_input(
            idempotency.key, idempotency.request_fingerprint);
        !valid) {
        return err(valid.error());
    }
    if (!command.user_id.is_valid() ||
        !command.original_transaction_id.is_valid() ||
        !command.account_id.is_valid() ||
        (command.category_id.has_value() && !command.category_id->is_valid())) {
        return err(Error::validation(
            "User, transaction, account, and category ids must be valid"));
    }
    if (command.type != domain::TransactionType::Income &&
        command.type != domain::TransactionType::Expense &&
        command.type != domain::TransactionType::Adjustment) {
        return err(Error::validation(
            "A correction replacement must be income, expense, or adjustment"));
    }
    if (command.description.size() > kMaxDescriptionLength) {
        return err(Error::validation(
            "description exceeds the maximum length"));
    }
    if (command.tag_ids.size() > domain::kMaxTagsPerTransaction) {
        return err(Error::validation(
            "A transaction can have at most 64 tags"));
    }
    std::set<std::int64_t> unique_tag_ids;
    for (const auto tag_id : command.tag_ids) {
        if (!tag_id.is_valid() ||
            !unique_tag_ids.insert(tag_id.value()).second) {
            return err(Error::validation(
                "tagIds must contain unique positive integers"));
        }
    }
    if (!is_plain_decimal_string(command.amount, true)) {
        return err(Error::validation(
            "amount must be a plain decimal string"));
    }
    auto amount = domain::Decimal::parse_numeric_20_8(command.amount);
    if (!amount) return err(from_domain(amount.error()));
    if (command.type == domain::TransactionType::Adjustment) {
        if (amount->is_zero()) {
            return err(Error::validation(
                "adjustment amount must be non-zero"));
        }
    } else if (!amount->is_positive()) {
        return err(Error::validation(
            "amount must be a positive decimal string"));
    }
    auto currency = domain::Currency::create(command.currency_code);
    if (!currency) return err(from_domain(currency.error()));

    std::optional<TransactionDto> result;
    std::optional<Error> app_error;
    const auto now = normalize_persisted_time(clock_.now());
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto started = idempotency_->begin(
                tx,
                command.user_id,
                "correct_transaction",
                idempotency.key,
                idempotency.request_fingerprint,
                idempotency.created_at,
                idempotency.created_at + kIdempotencyLifetime);
            if (!started) return std::unexpected(started.error());
            if (started->replay) {
                auto restored = transaction_from_idempotency_values(
                    started->response_values);
                if (!restored) {
                    app_error = restored.error();
                    return std::unexpected(domain::RepositoryError::database(
                        "Stored correction response is invalid"));
                }
                result = std::move(*restored);
                return {};
            }

            auto original = transactions_.find_by_id_for_update(
                tx, command.original_transaction_id);
            if (!original) return std::unexpected(original.error());
            if (original->user_id() != command.user_id) {
                app_error = Error::not_found(
                    "Transaction", command.original_transaction_id.to_string());
                return std::unexpected(domain::RepositoryError::not_found(
                    "Transaction not found for user"));
            }
            if (original->is_deleted()) {
                app_error = Error::conflict(
                    "Deleted or already corrected transactions cannot be corrected");
                return std::unexpected(domain::RepositoryError::conflict(
                    "Transaction is not active"));
            }
            if (original->type() == domain::TransactionType::Transfer ||
                original->transfer_group_id().has_value()) {
                app_error = Error::domain_rule_violation(
                    "Transfer aggregate members must be corrected as one transfer");
                return std::unexpected(domain::RepositoryError::validation(
                    "Transfer aggregate member"));
            }

            std::vector<domain::AccountId> account_ids{
                original->account_id(), command.account_id};
            std::sort(account_ids.begin(), account_ids.end());
            account_ids.erase(
                std::unique(account_ids.begin(), account_ids.end()),
                account_ids.end());
            std::optional<domain::Account> replacement_account;
            for (const auto account_id : account_ids) {
                auto account = accounts_.find_by_id_for_update(
                    tx, account_id, command.user_id);
                if (!account) return std::unexpected(account.error());
                if (account_id == command.account_id) {
                    replacement_account = *account;
                }
            }
            if (!replacement_account.has_value()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Replacement account lock was not acquired"));
            }
            if (replacement_account->is_archived() &&
                replacement_account->id() != original->account_id()) {
                app_error = Error(
                    ErrorCode::ArchivedAccountOperation,
                    "Cannot move a correction to an archived account");
                return std::unexpected(domain::RepositoryError::validation(
                    "Archived replacement account"));
            }
            if (!(replacement_account->currency() == *currency)) {
                app_error = Error::domain_rule_violation(
                    "Transaction currency must match account currency");
                return std::unexpected(domain::RepositoryError::validation(
                    "Currency mismatch"));
            }

            std::optional<std::string> category_name;
            if (command.category_id.has_value()) {
                auto category = categories_.find_by_id_for_user_for_update(
                    tx, *command.category_id, command.user_id);
                if (!category) return std::unexpected(category.error());
                auto board = domain::Category::validate_category_board(
                    command.type, category->board());
                if (!board) {
                    app_error = from_domain(board.error());
                    return std::unexpected(domain::RepositoryError::validation(
                        "Category board mismatch"));
                }
                category_name = category->name();
            }

            const auto occurred_at = normalize_persisted_time(
                command.occurred_at.value_or(now));
            domain::Transaction replacement(
                domain::TransactionId{},
                command.user_id,
                command.account_id,
                domain::Money(*amount, *currency),
                command.type,
                occurred_at,
                command.description,
                command.category_id,
                std::nullopt,
                now);
            auto persisted = transactions_.save_correction(
                tx, command.original_transaction_id, replacement, now);
            if (!persisted) return std::unexpected(persisted.error());

            auto tags = tags_.replace_transaction_tags(
                tx,
                persisted->replacement.id(),
                command.user_id,
                command.tag_ids);
            if (!tags) return std::unexpected(tags.error());

            result = to_transaction_dto(persisted->replacement);
            result->category_name = category_name;
            result->corrects_transaction_id = command.original_transaction_id;
            result->tags.reserve(tags->size());
            for (const auto& tag : *tags) {
                result->tags.push_back(TransactionTagDto{
                    tag.id(), tag.name(), tag.is_deleted()});
            }

            domain::AuditLogEntry audit;
            audit.operator_user_id = command.user_id;
            audit.action = domain::AuditAction::Update;
            audit.resource_type = "TransactionCorrection";
            audit.resource_id = command.original_transaction_id.to_string();
            audit.before_value_json =
                "{\"transactionId\":" +
                std::to_string(original->id().value()) +
                ",\"accountId\":" +
                std::to_string(original->account_id().value()) +
                ",\"amount\":" + domain::event_detail::json_string(
                    original->amount().amount().to_string()) + "}";
            audit.after_value_json =
                "{\"replacementTransactionId\":" +
                std::to_string(result->id.value()) +
                ",\"accountId\":" +
                std::to_string(result->account_id.value()) +
                ",\"amount\":" +
                domain::event_detail::json_string(result->amount) + "}";
            audit.metadata_json = "{}";
            audit.occurred_at = now;
            if (auto appended = audit_logs_.append(tx, audit); !appended) {
                return appended;
            }

            auto completed = idempotency_->complete(
                tx,
                command.user_id,
                "correct_transaction",
                idempotency.key,
                transaction_to_idempotency_values(*result));
            if (!completed) return completed;

            uow_.register_event(
                std::make_shared<domain::TransactionCorrectedEvent>(
                    command.user_id,
                    command.original_transaction_id,
                    result->id,
                    original->account_id(),
                    result->account_id,
                    now));
            return {};
        });
    if (!write) {
        if (app_error.has_value()) return err(*app_error);
        return err(from_repository(write.error()));
    }
    if (!result.has_value()) {
        return err(Error::infrastructure_failure(
            "Transaction correction result was not produced"));
    }
    return *result;
}

} // namespace pfh::application
