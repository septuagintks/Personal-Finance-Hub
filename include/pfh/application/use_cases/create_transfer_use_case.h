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
#include <chrono>
#include <memory>
#include <optional>
#include <utility>

namespace pfh::application {

class CreateTransferUseCase {
public:
    CreateTransferUseCase(
        domain::IAccountRepository& accounts,
        domain::ITransactionRepository& transactions,
        IUnitOfWork& uow)
        : accounts_(accounts), transactions_(transactions), uow_(uow) {}

    [[nodiscard]] Result<TransferResultDto> execute(const CreateTransferCommand& cmd) {
        if (cmd.source_account_id == cmd.target_account_id) {
            return err(Error::validation("source and target accounts must differ"));
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
                // Lock in ascending account id order to avoid deadlock cycles.
                const bool source_first =
                    cmd.source_account_id.value() <= cmd.target_account_id.value();
                const auto first_id =
                    source_first ? cmd.source_account_id : cmd.target_account_id;
                const auto second_id =
                    source_first ? cmd.target_account_id : cmd.source_account_id;

                auto first = accounts_.find_by_id_for_update(tx_ctx, first_id, cmd.user_id);
                if (!first) {
                    return std::unexpected(first.error());
                }
                auto second = accounts_.find_by_id_for_update(tx_ctx, second_id, cmd.user_id);
                if (!second) {
                    return std::unexpected(second.error());
                }
                const domain::Account& source = source_first ? *first : *second;
                const domain::Account& target = source_first ? *second : *first;

                if (source.is_archived() || target.is_archived()) {
                    app_error = Error(ErrorCode::ArchivedAccountOperation,
                                      "Cannot transfer involving archived account");
                    return std::unexpected(domain::RepositoryError::validation(
                        "archived account"));
                }

                auto aggregate_result = build_aggregate(cmd, source, target);
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
        return dto;
    }

private:
    [[nodiscard]] Result<domain::TransferAggregate> build_aggregate(
        const CreateTransferCommand& cmd,
        const domain::Account& source,
        const domain::Account& target) const {
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
            auto d = domain::Decimal::parse(amount);
            if (!d) {
                return err(from_domain(d.error()));
            }
            if (!d->is_positive()) {
                return err(Error::validation("amount must be positive"));
            }
            return domain::Money(*d, currency);
        };

        switch (cmd.mode) {
        case TransferInputMode::OutgoingAndRate: {
            auto out = parse_money(cmd.outgoing_amount, source.currency());
            if (!out) {
                return err(out.error());
            }
            auto rate_dec = domain::Decimal::parse(cmd.rate);
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
                domain::TransferGroupId{}));
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
                domain::TransferGroupId{}));
        }
        case TransferInputMode::IncomingAndRate: {
            auto in = parse_money(cmd.incoming_amount, target.currency());
            if (!in) {
                return err(in.error());
            }
            auto rate_dec = domain::Decimal::parse(cmd.rate);
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
                domain::TransferGroupId{}));
        }
        }
        return err(Error::validation("unsupported transfer mode"));
    }

    domain::IAccountRepository& accounts_;
    domain::ITransactionRepository& transactions_;
    IUnitOfWork& uow_;
};

} // namespace pfh::application
