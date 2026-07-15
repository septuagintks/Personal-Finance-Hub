// Personal Finance Hub - CreateTransferUseCase
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
#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/domain/transfer_domain_service.h"
#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace pfh::application {

class CreateTransferUseCase {
public:
    CreateTransferUseCase(
        domain::IAccountRepository& accounts,
        domain::ITransactionRepository& transactions,
        IUnitOfWork& uow,
        IIdempotencyRepository* idempotency = nullptr)
        : accounts_(accounts), transactions_(transactions), uow_(uow),
          idempotency_(idempotency) {}

    [[nodiscard]] Result<TransferResultDto> execute(const CreateTransferCommand& cmd) {
        return execute_impl(cmd, std::nullopt);
    }

    [[nodiscard]] Result<TransferResultDto> execute(
        const CreateTransferCommand& cmd,
        const IdempotencyRequest& idempotency) {
        return execute_impl(cmd, idempotency);
    }

private:
    [[nodiscard]] Result<TransferResultDto> execute_impl(
        const CreateTransferCommand& cmd,
        const std::optional<IdempotencyRequest>& idempotency) {
        if (idempotency.has_value()) {
            if (idempotency_ == nullptr) {
                return err(Error::infrastructure_failure(
                    "Transfer idempotency is unavailable"));
            }
            if (auto valid = validate_idempotency_input(
                    idempotency->key, idempotency->request_fingerprint);
                !valid) {
                return err(valid.error());
            }
        }
        if (auto shape = validate_command_shape(cmd); !shape) {
            return err(shape.error());
        }

        // Everything that reads the accounts, validates them and writes the
        // transfer happens inside ONE transaction. The account rows are locked
        // with find_by_id_for_update (SELECT ... FOR UPDATE) in ascending id
        // order to prevent deadlocks (design §4.1), and the leg ids come back
        // from save_transfer instead of a non-transactional re-read that a real
        // PostgreSQL connection would not see.
        std::optional<domain::TransferAggregate> aggregate;
        domain::TransferPersistResult persisted;
        std::optional<TransferResultDto> result_dto;
        // Carries an application-level error (with its precise ErrorCode) out of
        // the transaction closure. The closure can only signal abort via a
        // RepositoryError, which would flatten codes like ArchivedAccountOperation
        // into a generic 400/500; capturing the real Error here preserves it.
        std::optional<Error> app_error;
        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx_ctx) -> domain::RepositoryVoidResult {
                if (idempotency.has_value()) {
                    auto started = idempotency_->begin(
                        tx_ctx,
                        cmd.user_id,
                        "create_transfer",
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

                // Lock every affected account in ascending id order. This
                // includes a third-party fee account, preventing three-row
                // lock cycles when concurrent transfers overlap.
                std::vector<domain::AccountId> lock_ids{
                    cmd.source_account_id, cmd.target_account_id};
                if (cmd.fee_source == domain::FeeSource::ThirdParty) {
                    lock_ids.push_back(*cmd.fee_account_id);
                }
                std::sort(lock_ids.begin(), lock_ids.end(), [](const auto lhs, const auto rhs) {
                    return lhs.value() < rhs.value();
                });
                lock_ids.erase(std::unique(lock_ids.begin(), lock_ids.end()), lock_ids.end());

                std::vector<domain::Account> locked_accounts;
                locked_accounts.reserve(lock_ids.size());
                for (const auto account_id : lock_ids) {
                    auto account = accounts_.find_by_id_for_update(
                        tx_ctx, account_id, cmd.user_id);
                    if (!account) {
                        return std::unexpected(account.error());
                    }
                    locked_accounts.push_back(std::move(*account));
                }

                const auto find_locked = [&](domain::AccountId id) -> const domain::Account* {
                    const auto found = std::find_if(
                        locked_accounts.begin(),
                        locked_accounts.end(),
                        [id](const domain::Account& account) { return account.id() == id; });
                    return found == locked_accounts.end() ? nullptr : &*found;
                };
                const domain::Account* source = find_locked(cmd.source_account_id);
                const domain::Account* target = find_locked(cmd.target_account_id);
                const domain::Account* third_party_fee_account =
                    cmd.fee_source == domain::FeeSource::ThirdParty
                    ? find_locked(*cmd.fee_account_id)
                    : nullptr;

                if (source == nullptr || target == nullptr ||
                    (cmd.fee_source == domain::FeeSource::ThirdParty &&
                     third_party_fee_account == nullptr)) {
                    return std::unexpected(domain::RepositoryError::database(
                        "Locked transfer account could not be resolved"));
                }

                if (source->is_archived() || target->is_archived() ||
                    (third_party_fee_account != nullptr &&
                     third_party_fee_account->is_archived())) {
                    app_error = Error(ErrorCode::ArchivedAccountOperation,
                                      "Cannot transfer or charge a fee using an archived account");
                    return std::unexpected(domain::RepositoryError::validation(
                        "archived account"));
                }

                auto aggregate_result = build_aggregate(
                    cmd, *source, *target, third_party_fee_account);
                if (!aggregate_result) {
                    app_error = aggregate_result.error();
                    return std::unexpected(domain::RepositoryError::validation(
                        "invalid transfer"));
                }
                aggregate = std::move(*aggregate_result);

                auto saved = transactions_.save_transfer(tx_ctx, *aggregate);
                if (!saved) {
                    return std::unexpected(saved.error());
                }
                persisted = *saved;
                result_dto = to_dto(persisted, *aggregate);
                if (idempotency.has_value()) {
                    auto completed = idempotency_->complete(
                        tx_ctx,
                        cmd.user_id,
                        "create_transfer",
                        idempotency->key,
                        to_idempotency_values(*result_dto));
                    if (!completed) {
                        return completed;
                    }
                }

                uow_.register_event(std::make_shared<domain::TransferCompletedEvent>(
                    cmd.user_id,
                    persisted.group_id,
                    cmd.source_account_id,
                    cmd.target_account_id,
                    aggregate->outgoing().occurred_at()));
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
                "Transfer result was not produced"));
        }
        return *result_dto;
    }

    [[nodiscard]] static TransferResultDto to_dto(
        const domain::TransferPersistResult& persisted,
        const domain::TransferAggregate& aggregate) {
        TransferResultDto dto;
        dto.transfer_group_id = persisted.group_id;
        dto.outgoing_transaction_id = persisted.outgoing_id;
        dto.incoming_transaction_id = persisted.incoming_id;
        dto.outgoing_amount =
            aggregate.outgoing().amount().amount().to_string();
        dto.incoming_amount =
            aggregate.incoming().amount().amount().to_string();
        if (aggregate.rate().has_value()) {
            dto.rate = aggregate.rate()->rate().to_string();
        }
        if (!aggregate.adjustments().empty()) {
            dto.fee_amount = aggregate.adjustments().front().amount().amount().abs().to_string();
        }
        return dto;
    }

    [[nodiscard]] static IdempotencyValues to_idempotency_values(
        const TransferResultDto& value) {
        return {
            {"transfer_group_id", value.transfer_group_id.to_string()},
            {"outgoing_transaction_id", value.outgoing_transaction_id.to_string()},
            {"incoming_transaction_id", value.incoming_transaction_id.to_string()},
            {"outgoing_amount", value.outgoing_amount},
            {"incoming_amount", value.incoming_amount},
            {"rate", value.rate.value_or("")},
            {"fee_amount", value.fee_amount.value_or("")}};
    }

    [[nodiscard]] static Result<TransferResultDto> from_idempotency_values(
        const IdempotencyValues& values) {
        const auto group = idempotency_value(values, "transfer_group_id");
        const auto outgoing_id = idempotency_value(
            values, "outgoing_transaction_id");
        const auto incoming_id = idempotency_value(
            values, "incoming_transaction_id");
        const auto outgoing = idempotency_value(values, "outgoing_amount");
        const auto incoming = idempotency_value(values, "incoming_amount");
        const auto rate = idempotency_value(values, "rate");
        const auto fee = idempotency_value(values, "fee_amount");
        if (!group || !outgoing_id || !incoming_id || !outgoing || !incoming ||
            !rate || !fee) {
            return err(Error::infrastructure_failure(
                "Stored transfer response is incomplete"));
        }
        const auto group_value = parse_idempotency_integer(*group);
        const auto outgoing_value = parse_idempotency_integer(*outgoing_id);
        const auto incoming_value = parse_idempotency_integer(*incoming_id);
        if (!group_value || !outgoing_value || !incoming_value ||
            *group_value <= 0 || *outgoing_value <= 0 || *incoming_value <= 0) {
            return err(Error::infrastructure_failure(
                "Stored transfer response is invalid"));
        }
        TransferResultDto result;
        result.transfer_group_id = domain::TransferGroupId(*group_value);
        result.outgoing_transaction_id = domain::TransactionId(*outgoing_value);
        result.incoming_transaction_id = domain::TransactionId(*incoming_value);
        result.outgoing_amount = std::string(*outgoing);
        result.incoming_amount = std::string(*incoming);
        if (!rate->empty()) result.rate = std::string(*rate);
        if (!fee->empty()) result.fee_amount = std::string(*fee);
        return result;
    }

    [[nodiscard]] static VoidResult validate_command_shape(
        const CreateTransferCommand& cmd) {
        if (!cmd.user_id.is_valid() || !cmd.source_account_id.is_valid() ||
            !cmd.target_account_id.is_valid()) {
            return err(Error::validation("user and transfer account ids must be valid"));
        }
        if (cmd.description.size() > kMaxDescriptionLength) {
            return err(Error::validation(
                "description exceeds the maximum length"));
        }
        if (cmd.source_account_id == cmd.target_account_id) {
            return err(Error::validation("source and target accounts must differ"));
        }

        switch (cmd.mode) {
        case TransferInputMode::OutgoingAndRate:
            if (cmd.outgoing_amount.empty() || cmd.rate.empty()) {
                return err(Error::validation(
                    "OutgoingAndRate requires outgoing_amount and rate"));
            }
            if (!cmd.incoming_amount.empty()) {
                return err(Error::validation(
                    "OutgoingAndRate must not provide incoming_amount"));
            }
            break;
        case TransferInputMode::BothAmounts:
            if (cmd.outgoing_amount.empty() || cmd.incoming_amount.empty()) {
                return err(Error::validation(
                    "BothAmounts requires outgoing_amount and incoming_amount"));
            }
            if (!cmd.rate.empty()) {
                return err(Error::validation("BothAmounts must not provide rate"));
            }
            break;
        case TransferInputMode::IncomingAndRate:
            if (cmd.incoming_amount.empty() || cmd.rate.empty()) {
                return err(Error::validation(
                    "IncomingAndRate requires incoming_amount and rate"));
            }
            if (!cmd.outgoing_amount.empty()) {
                return err(Error::validation(
                    "IncomingAndRate must not provide outgoing_amount"));
            }
            break;
        default:
            return err(Error::validation("unsupported transfer mode"));
        }

        if (!cmd.fee_amount.has_value()) {
            if (cmd.fee_source.has_value() || cmd.fee_account_id.has_value()) {
                return err(Error::validation(
                    "fee_source and fee_account_id require fee_amount"));
            }
            return ok();
        }
        if (!cmd.fee_source.has_value()) {
            return err(Error::validation("fee_amount requires fee_source"));
        }

        switch (*cmd.fee_source) {
        case domain::FeeSource::SourceAccount:
        case domain::FeeSource::TargetAccount:
            if (cmd.fee_account_id.has_value()) {
                return err(Error::validation(
                    "fee_account_id is only valid for ThirdParty fees"));
            }
            break;
        case domain::FeeSource::ThirdParty:
            if (!cmd.fee_account_id.has_value() || !cmd.fee_account_id->is_valid()) {
                return err(Error::validation(
                    "ThirdParty fee requires a valid fee_account_id"));
            }
            if (*cmd.fee_account_id == cmd.source_account_id ||
                *cmd.fee_account_id == cmd.target_account_id) {
                return err(Error::validation(
                    "ThirdParty fee account must differ from both transfer accounts"));
            }
            break;
        default:
            return err(Error::validation("unsupported fee source"));
        }
        return ok();
    }

    [[nodiscard]] Result<domain::TransferAggregate> build_aggregate(
        const CreateTransferCommand& cmd,
        const domain::Account& source,
        const domain::Account& target,
        const domain::Account* third_party_fee_account) const {
        using domain::TransferDomainService;

        // Stamp current time when the caller omitted a business time, so a
        // missing REST field never lands the transfer legs in 1970.
        const auto occurred_at =
            cmd.occurred_at.value_or(std::chrono::system_clock::now());

        auto parse_money = [](const std::string& amount,
                              const domain::Currency& currency)
            -> Result<domain::Money> {
            if (!is_plain_decimal_string(amount, false)) {
                return err(Error::validation(
                    "amount must be a positive plain decimal string"));
            }
            auto d = domain::Decimal::parse_numeric_20_8(amount);
            if (!d) {
                return err(from_domain(d.error()));
            }
            if (!d->is_positive()) {
                return err(Error::validation("amount must be positive"));
            }
            return domain::Money(*d, currency);
        };

        std::optional<domain::TransferFee> fee;
        if (cmd.fee_amount.has_value()) {
            const domain::Account* selected_fee_account = nullptr;
            switch (*cmd.fee_source) {
            case domain::FeeSource::SourceAccount:
                selected_fee_account = &source;
                break;
            case domain::FeeSource::TargetAccount:
                selected_fee_account = &target;
                break;
            case domain::FeeSource::ThirdParty:
                selected_fee_account = third_party_fee_account;
                break;
            }
            if (selected_fee_account == nullptr) {
                return err(Error::validation("fee account could not be resolved"));
            }
            auto amount = parse_money(*cmd.fee_amount, selected_fee_account->currency());
            if (!amount) {
                return err(amount.error());
            }
            fee = domain::TransferFee{
                *cmd.fee_source, selected_fee_account->id(), std::move(*amount)};
        }

        switch (cmd.mode) {
        case TransferInputMode::OutgoingAndRate: {
            auto out = parse_money(cmd.outgoing_amount, source.currency());
            if (!out) {
                return err(out.error());
            }
            if (!is_plain_decimal_string(cmd.rate, false)) {
                return err(Error::validation(
                    "rate must be a positive plain decimal string"));
            }
            auto rate_dec = domain::Decimal::parse_numeric_20_10(cmd.rate);
            if (!rate_dec) {
                return err(from_domain(rate_dec.error()));
            }
            auto rate = domain::ExchangeRate::create(
                source.currency(),
                target.currency(),
                *rate_dec,
                occurred_at,
                "Manual");
            if (!rate) {
                return err(from_domain(rate.error()));
            }
            return map_domain(TransferDomainService::build_from_outgoing_and_rate(
                *out,
                source.id(),
                target.id(),
                *rate,
                cmd.user_id,
                occurred_at,
                cmd.description,
                domain::TransferGroupId{},
                fee));
        }
        case TransferInputMode::BothAmounts: {
            auto out = parse_money(cmd.outgoing_amount, source.currency());
            if (!out) {
                return err(out.error());
            }
            auto in = parse_money(cmd.incoming_amount, target.currency());
            if (!in) {
                return err(in.error());
            }
            return map_domain(TransferDomainService::build_from_both_amounts(
                *out,
                *in,
                source.id(),
                target.id(),
                cmd.user_id,
                occurred_at,
                cmd.description,
                domain::TransferGroupId{},
                fee));
        }
        case TransferInputMode::IncomingAndRate: {
            auto in = parse_money(cmd.incoming_amount, target.currency());
            if (!in) {
                return err(in.error());
            }
            if (!is_plain_decimal_string(cmd.rate, false)) {
                return err(Error::validation(
                    "rate must be a positive plain decimal string"));
            }
            auto rate_dec = domain::Decimal::parse_numeric_20_10(cmd.rate);
            if (!rate_dec) {
                return err(from_domain(rate_dec.error()));
            }
            auto rate = domain::ExchangeRate::create(
                source.currency(),
                target.currency(),
                *rate_dec,
                occurred_at,
                "Manual");
            if (!rate) {
                return err(from_domain(rate.error()));
            }
            return map_domain(TransferDomainService::build_from_incoming_and_rate(
                *in,
                source.id(),
                target.id(),
                *rate,
                cmd.user_id,
                occurred_at,
                cmd.description,
                domain::TransferGroupId{},
                fee));
        }
        }
        return err(Error::validation("unsupported transfer mode"));
    }

    domain::IAccountRepository& accounts_;
    domain::ITransactionRepository& transactions_;
    IUnitOfWork& uow_;
    IIdempotencyRepository* idempotency_;
};

} // namespace pfh::application
