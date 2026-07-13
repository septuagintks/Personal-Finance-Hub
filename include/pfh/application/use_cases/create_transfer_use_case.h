// Personal Finance Hub - CreateTransferUseCase
// Version: 1.0
// C++23

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/persistence/i_unit_of_work.h"
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
        IUnitOfWork& uow)
        : accounts_(accounts), transactions_(transactions), uow_(uow) {}

    [[nodiscard]] Result<TransferResultDto> execute(const CreateTransferCommand& cmd) {
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
        // Carries an application-level error (with its precise ErrorCode) out of
        // the transaction closure. The closure can only signal abort via a
        // RepositoryError, which would flatten codes like ArchivedAccountOperation
        // into a generic 400/500; capturing the real Error here preserves it.
        std::optional<Error> app_error;
        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx_ctx) -> domain::RepositoryVoidResult {
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

        TransferResultDto dto;
        dto.transfer_group_id = persisted.group_id;
        dto.outgoing_transaction_id = persisted.outgoing_id;
        dto.incoming_transaction_id = persisted.incoming_id;
        dto.outgoing_amount = aggregate->outgoing().amount().to_string();
        dto.incoming_amount = aggregate->incoming().amount().to_string();
        if (aggregate->rate().has_value()) {
            dto.rate = aggregate->rate()->rate().to_string();
        }
        if (!aggregate->adjustments().empty()) {
            dto.fee_amount = aggregate->adjustments().front().amount().amount().abs().to_string();
        }
        return dto;
    }

private:
    [[nodiscard]] static VoidResult validate_command_shape(
        const CreateTransferCommand& cmd) {
        if (!cmd.user_id.is_valid() || !cmd.source_account_id.is_valid() ||
            !cmd.target_account_id.is_valid()) {
            return err(Error::validation("user and transfer account ids must be valid"));
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
            if (amount.empty()) {
                return err(Error::validation("amount is required"));
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
};

} // namespace pfh::application
