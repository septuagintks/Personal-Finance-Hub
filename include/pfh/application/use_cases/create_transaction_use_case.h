// Personal Finance Hub - CreateTransactionUseCase
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
#include <memory>
#include <utility>

namespace pfh::application {

class CreateTransactionUseCase {
public:
    CreateTransactionUseCase(
        domain::IAccountRepository& accounts,
        domain::ITransactionRepository& transactions,
        IUnitOfWork& uow)
        : accounts_(accounts), transactions_(transactions), uow_(uow) {}

    [[nodiscard]] Result<TransactionDto> execute(const CreateTransactionCommand& cmd) {
        if (cmd.amount.empty()) {
            return err(Error::validation("amount is required"));
        }
        if (cmd.type == domain::TransactionType::Transfer) {
            return err(Error::validation(
                "Use CreateTransferUseCase for transfer transactions"));
        }

        auto amount_dec = domain::Decimal::parse(cmd.amount);
        if (!amount_dec) {
            return err(from_domain(amount_dec.error()));
        }
        if (!amount_dec->is_positive()) {
            return err(Error::validation("amount must be a positive decimal string"));
        }

        auto currency = domain::Currency::create(cmd.currency_code);
        if (!currency) {
            return err(from_domain(currency.error()));
        }

        auto account = accounts_.find_by_id_for_user(cmd.account_id, cmd.user_id);
        if (!account) {
            return err(from_repository(account.error()));
        }
        if (account->is_archived()) {
            return err(Error(ErrorCode::ArchivedAccountOperation,
                             "Cannot post to archived account"));
        }
        if (!(account->currency() == *currency)) {
            return err(Error::domain_rule_violation(
                "Transaction currency must match account currency",
                account->currency().code() + " != " + currency->code()));
        }

        domain::Money money(*amount_dec, *currency);
        domain::Transaction tx(
            domain::TransactionId{},
            cmd.user_id,
            cmd.account_id,
            money,
            cmd.type,
            cmd.occurred_at,
            cmd.description,
            cmd.category_id);

        domain::TransactionId created_id;
        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx_ctx) -> domain::RepositoryVoidResult {
                auto saved = transactions_.save_single(tx_ctx, tx);
                if (!saved) {
                    return std::unexpected(saved.error());
                }
                created_id = *saved;
                uow_.register_event(std::make_shared<domain::SimpleDomainEvent>(
                    "TransactionCreated",
                    "Transaction",
                    created_id.to_string(),
                    "{\"account_id\":" + cmd.account_id.to_string() + "}"));
                return {};
            });
        if (!write) {
            return err(from_repository(write.error()));
        }

        auto stored = transactions_.find_by_id(created_id);
        if (!stored) {
            return err(from_repository(stored.error()));
        }
        return to_dto(*stored);
    }

private:
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
    domain::ITransactionRepository& transactions_;
    IUnitOfWork& uow_;
};

} // namespace pfh::application
