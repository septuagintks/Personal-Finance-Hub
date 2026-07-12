// Personal Finance Hub - CreateTransactionUseCase
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

        // Category board rule: if a category is attached, its board must match
        // the transaction type (Income->income, Expense/Adjustment->expense).
        // This is pure input validation, so it stays outside the transaction.
        if (cmd.category_id.has_value()) {
            if (!cmd.category_board.has_value()) {
                return err(Error::validation(
                    "category_board is required when category_id is provided"));
            }
            auto board_check = domain::Category::validate_category_board(
                cmd.type, *cmd.category_board);
            if (!board_check) {
                return err(from_domain(board_check.error()));
            }
        }

        domain::Money money(*amount_dec, *currency);

        // Account load+validate and the write share one transaction, with the
        // account row locked (SELECT ... FOR UPDATE) so a concurrent archive or
        // currency change cannot slip between the check and the insert.
        std::optional<domain::Transaction> persisted;
        std::optional<Error> app_error;
        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx_ctx) -> domain::RepositoryVoidResult {
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
                persisted = std::move(*saved);
                uow_.register_event(std::make_shared<domain::TransactionCreatedEvent>(
                    cmd.user_id,
                    persisted->id(),
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

        return to_dto(*persisted);
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
