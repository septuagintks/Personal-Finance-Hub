// Personal Finance Hub - CreateTransactionUseCase
// Version: 1.0
// C++23

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/idempotency.h"
#include "pfh/application/input_constraints.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/application/ports/i_idempotency_repository.h"
#include "pfh/domain/events/domain_events.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_category_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/domain/category.h"
#include <chrono>
#include <memory>
#include <optional>
#include <utility>

namespace pfh::application {

class CreateTransactionUseCase {
public:
    CreateTransactionUseCase(
        domain::IAccountRepository& accounts,
        domain::ICategoryRepository& categories,
        domain::ITransactionRepository& transactions,
        IUnitOfWork& uow,
        IIdempotencyRepository* idempotency = nullptr)
        : accounts_(accounts), categories_(categories),
          transactions_(transactions), uow_(uow), idempotency_(idempotency) {}

    [[nodiscard]] Result<TransactionDto> execute(const CreateTransactionCommand& cmd) {
        return execute_impl(cmd, std::nullopt);
    }

    [[nodiscard]] Result<TransactionDto> execute(
        const CreateTransactionCommand& cmd,
        const IdempotencyRequest& idempotency) {
        return execute_impl(cmd, idempotency);
    }

private:
    [[nodiscard]] Result<TransactionDto> execute_impl(
        const CreateTransactionCommand& cmd,
        const std::optional<IdempotencyRequest>& idempotency) {
        if (idempotency.has_value()) {
            if (idempotency_ == nullptr) {
                return err(Error::infrastructure_failure(
                    "Transaction idempotency is unavailable"));
            }
            if (auto valid = validate_idempotency_input(
                    idempotency->key, idempotency->request_fingerprint);
                !valid) {
                return err(valid.error());
            }
        }
        if (!cmd.user_id.is_valid() || !cmd.account_id.is_valid() ||
            (cmd.category_id.has_value() && !cmd.category_id->is_valid())) {
            return err(Error::validation(
                "user, account, and category ids must be valid"));
        }
        if (cmd.type != domain::TransactionType::Income &&
            cmd.type != domain::TransactionType::Expense &&
            cmd.type != domain::TransactionType::Adjustment) {
            return err(Error::validation("unsupported transaction type"));
        }
        if (cmd.description.size() > kMaxDescriptionLength) {
            return err(Error::validation(
                "description exceeds the maximum length"));
        }
        if (!is_plain_decimal_string(cmd.amount, true)) {
            return err(Error::validation(
                "amount must be a plain decimal string"));
        }
        if (cmd.type == domain::TransactionType::Transfer) {
            return err(Error::validation(
                "Use CreateTransferUseCase for transfer transactions"));
        }

        auto amount_dec = domain::Decimal::parse_numeric_20_8(cmd.amount);
        if (!amount_dec) {
            return err(from_domain(amount_dec.error()));
        }
        // Income and Expense are magnitudes and must be strictly positive; the
        // stored sign is derived from the type. Adjustment is SIGNED and may be
        // positive (refund/subsidy/FX gain) or negative (fee/correction/FX
        // loss), but never zero (a no-op adjustment is meaningless).
        if (cmd.type == domain::TransactionType::Adjustment) {
            if (amount_dec->is_zero()) {
                return err(Error::validation("adjustment amount must be non-zero"));
            }
        } else if (!amount_dec->is_positive()) {
            return err(Error::validation("amount must be a positive decimal string"));
        }
        auto currency = domain::Currency::create(cmd.currency_code);
        if (!currency) {
            return err(from_domain(currency.error()));
        }

        domain::Money money(*amount_dec, *currency);

        // Account load+validate and the write share one transaction, with the
        // account row locked (SELECT ... FOR UPDATE) so a concurrent archive or
        // currency change cannot slip between the check and the insert.
        std::optional<TransactionDto> result_dto;
        std::optional<Error> app_error;
        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx_ctx) -> domain::RepositoryVoidResult {
                if (idempotency.has_value()) {
                    auto started = idempotency_->begin(
                        tx_ctx,
                        cmd.user_id,
                        "create_transaction",
                        idempotency->key,
                        idempotency->request_fingerprint,
                        idempotency->created_at,
                        idempotency->created_at + kIdempotencyLifetime);
                    if (!started) {
                        return std::unexpected(started.error());
                    }
                    if (started->replay) {
                        auto restored = from_idempotency_values(
                            started->response_values);
                        if (!restored) {
                            app_error = restored.error();
                            return std::unexpected(domain::RepositoryError::database(
                                "Stored idempotency response is invalid"));
                        }
                        result_dto = std::move(*restored);
                        return {};
                    }
                }

                auto account =
                    accounts_.find_by_id_for_update(tx_ctx, cmd.account_id, cmd.user_id);
                if (!account) {
                    return std::unexpected(account.error());
                }
                if (account->is_archived()) {
                    app_error = Error(ErrorCode::ArchivedAccountOperation,
                                      "Cannot post to archived account");
                    return std::unexpected(domain::RepositoryError::validation(
                        "archived account"));
                }
                if (!(account->currency() == *currency)) {
                    app_error = Error::domain_rule_violation(
                        "Transaction currency must match account currency",
                        account->currency().code() + " != " + currency->code());
                    return std::unexpected(domain::RepositoryError::validation(
                        "currency mismatch"));
                }

                if (cmd.category_id.has_value()) {
                    auto category = categories_.find_by_id_for_user_for_update(
                        tx_ctx, *cmd.category_id, cmd.user_id);
                    if (!category) {
                        return std::unexpected(category.error());
                    }
                    auto board_check = domain::Category::validate_category_board(
                        cmd.type, category->board());
                    if (!board_check) {
                        app_error = from_domain(board_check.error());
                        return std::unexpected(domain::RepositoryError::validation(
                            "category board mismatch"));
                    }
                }

                // Stamp current time when the caller omitted a business time,
                // so a missing REST field never lands the record in 1970.
                const auto occurred_at =
                    cmd.occurred_at.value_or(std::chrono::system_clock::now());
                domain::Transaction tx(
                    domain::TransactionId{},
                    cmd.user_id,
                    cmd.account_id,
                    money,
                    cmd.type,
                    occurred_at,
                    cmd.description,
                    cmd.category_id);

                // save_single returns the persisted entity, so we build the DTO
                // from it directly — no post-commit re-read (which an RLS-scoped
                // connection might not see, and whose failure would falsely fail
                // an already-committed write).
                auto saved = transactions_.save_single(tx_ctx, tx);
                if (!saved) {
                    return std::unexpected(saved.error());
                }
                result_dto = to_dto(*saved);
                if (idempotency.has_value()) {
                    auto completed = idempotency_->complete(
                        tx_ctx,
                        cmd.user_id,
                        "create_transaction",
                        idempotency->key,
                        to_idempotency_values(*result_dto));
                    if (!completed) {
                        return completed;
                    }
                }
                uow_.register_event(std::make_shared<domain::TransactionCreatedEvent>(
                    cmd.user_id,
                    result_dto->id,
                    cmd.account_id,
                    occurred_at));
                return {};
            });
        if (!write) {
            if (app_error.has_value()) {
                return err(*app_error);
            }
            return err(from_repository(write.error()));
        }

        if (!result_dto.has_value()) {
            return err(Error::infrastructure_failure(
                "Transaction result was not produced"));
        }
        return *result_dto;
    }

    [[nodiscard]] static IdempotencyValues to_idempotency_values(
        const TransactionDto& value) {
        return {
            {"id", value.id.to_string()},
            {"user_id", value.user_id.to_string()},
            {"account_id", value.account_id.to_string()},
            {"currency_code", value.currency_code},
            {"amount", value.amount},
            {"type", std::to_string(static_cast<int>(value.type))},
            {"description", value.description},
            {"category_id", value.category_id.has_value()
                ? value.category_id->to_string() : ""},
            {"transfer_group_id", value.transfer_group_id.has_value()
                ? value.transfer_group_id->to_string() : ""},
            {"occurred_at", encode_idempotency_time(value.occurred_at)}};
    }

    [[nodiscard]] static Result<TransactionDto> from_idempotency_values(
        const IdempotencyValues& values) {
        const auto id = idempotency_value(values, "id");
        const auto user_id = idempotency_value(values, "user_id");
        const auto account_id = idempotency_value(values, "account_id");
        const auto currency = idempotency_value(values, "currency_code");
        const auto amount = idempotency_value(values, "amount");
        const auto type = idempotency_value(values, "type");
        const auto description = idempotency_value(values, "description");
        const auto category = idempotency_value(values, "category_id");
        const auto transfer = idempotency_value(values, "transfer_group_id");
        const auto occurred_at = idempotency_value(values, "occurred_at");
        if (!id || !user_id || !account_id || !currency || !amount || !type ||
            !description || !category || !transfer || !occurred_at) {
            return err(Error::infrastructure_failure(
                "Stored transaction response is incomplete"));
        }
        const auto id_value = parse_idempotency_integer(*id);
        const auto user_value = parse_idempotency_integer(*user_id);
        const auto account_value = parse_idempotency_integer(*account_id);
        const auto type_value = parse_idempotency_integer(*type);
        const auto time_value = decode_idempotency_time(*occurred_at);
        if (!id_value || !user_value || !account_value || !type_value ||
            !time_value || *id_value <= 0 || *user_value <= 0 ||
            *account_value <= 0 || *type_value < 0 || *type_value > 3) {
            return err(Error::infrastructure_failure(
                "Stored transaction response is invalid"));
        }

        TransactionDto result;
        result.id = domain::TransactionId(*id_value);
        result.user_id = domain::UserId(*user_value);
        result.account_id = domain::AccountId(*account_value);
        result.currency_code = std::string(*currency);
        result.amount = std::string(*amount);
        result.type = static_cast<domain::TransactionType>(*type_value);
        result.description = std::string(*description);
        result.occurred_at = *time_value;
        if (!category->empty()) {
            const auto value = parse_idempotency_integer(*category);
            if (!value || *value <= 0) {
                return err(Error::infrastructure_failure(
                    "Stored transaction category is invalid"));
            }
            result.category_id = domain::CategoryId(*value);
        }
        if (!transfer->empty()) {
            const auto value = parse_idempotency_integer(*transfer);
            if (!value || *value <= 0) {
                return err(Error::infrastructure_failure(
                    "Stored transaction transfer group is invalid"));
            }
            result.transfer_group_id = domain::TransferGroupId(*value);
        }
        return result;
    }

    [[nodiscard]] static TransactionDto to_dto(const domain::Transaction& tx) {
        TransactionDto dto;
        dto.id = tx.id();
        dto.user_id = tx.user_id();
        dto.account_id = tx.account_id();
        dto.currency_code = tx.amount().currency().code();
        dto.amount = tx.amount().amount().to_string();
        dto.type = tx.type();
        dto.description = tx.description();
        dto.category_id = tx.category_id();
        dto.transfer_group_id = tx.transfer_group_id();
        dto.occurred_at = tx.occurred_at();
        return dto;
    }

    domain::IAccountRepository& accounts_;
    domain::ICategoryRepository& categories_;
    domain::ITransactionRepository& transactions_;
    IUnitOfWork& uow_;
    IIdempotencyRepository* idempotency_;
};

} // namespace pfh::application
