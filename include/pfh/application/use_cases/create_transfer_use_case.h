// Personal Finance Hub - CreateTransferUseCase
// Version: 1.0
// C++23

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/domain/events/simple_domain_event.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/domain/transfer_domain_service.h"
#include <memory>
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

        auto source = accounts_.find_by_id_for_user(cmd.source_account_id, cmd.user_id);
        if (!source) {
            return err(from_repository(source.error()));
        }
        auto target = accounts_.find_by_id_for_user(cmd.target_account_id, cmd.user_id);
        if (!target) {
            return err(from_repository(target.error()));
        }
        if (source->is_archived() || target->is_archived()) {
            return err(Error(ErrorCode::ArchivedAccountOperation,
                             "Cannot transfer involving archived account"));
        }

        auto aggregate_result = build_aggregate(cmd, *source, *target);
        if (!aggregate_result) {
            return err(aggregate_result.error());
        }
        const domain::TransferAggregate aggregate = std::move(*aggregate_result);

        domain::TransferGroupId group_id;
        domain::TransactionId out_id;
        domain::TransactionId in_id;
        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx_ctx) -> domain::RepositoryVoidResult {
                auto saved = transactions_.save_transfer(tx_ctx, aggregate);
                if (!saved) {
                    return std::unexpected(saved.error());
                }
                group_id = *saved;

                // Resolve persisted legs for response DTO.
                auto all = transactions_.find_by_user(cmd.user_id, false);
                if (!all) {
                    return std::unexpected(all.error());
                }
                for (const auto& tx : *all) {
                    if (!tx.transfer_group_id().has_value() ||
                        tx.transfer_group_id()->value() != group_id.value()) {
                        continue;
                    }
                    if (tx.account_id() == cmd.source_account_id) {
                        out_id = tx.id();
                    } else if (tx.account_id() == cmd.target_account_id) {
                        in_id = tx.id();
                    }
                }

                uow_.register_event(std::make_shared<domain::SimpleDomainEvent>(
                    "TransferCompleted",
                    "TransferGroup",
                    group_id.to_string(),
                    "{\"source\":" + cmd.source_account_id.to_string() +
                        ",\"target\":" + cmd.target_account_id.to_string() + "}"));
                return {};
            });
        if (!write) {
            return err(from_repository(write.error()));
        }

        TransferResultDto dto;
        dto.transfer_group_id = group_id;
        dto.outgoing_transaction_id = out_id;
        dto.incoming_transaction_id = in_id;
        dto.outgoing_amount = aggregate.outgoing().amount().to_string();
        dto.incoming_amount = aggregate.incoming().amount().to_string();
        if (aggregate.rate().has_value()) {
            dto.rate = aggregate.rate()->rate().to_string();
        }
        return dto;
    }

private:
    [[nodiscard]] Result<domain::TransferAggregate> build_aggregate(
        const CreateTransferCommand& cmd,
        const domain::Account& source,
        const domain::Account& target) const {
        using domain::TransferDomainService;

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
                cmd.occurred_at,
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
                cmd.occurred_at,
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
                cmd.occurred_at,
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
                cmd.occurred_at,
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
                cmd.occurred_at,
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
