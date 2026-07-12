// Personal Finance Hub - DeleteAccountUseCase (dangerous physical delete)
// Version: 1.0
// C++23
//
// Backs REST DELETE /api/v1/accounts/{id}?confirmations=3 (10_REST_API_Design
// §2.3). This is an irreversible physical purge, so it demands an explicit
// confirmation count and performs all deletes inside one transaction.

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/domain/events/simple_domain_event.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include <memory>
#include <optional>

namespace pfh::application {

struct DeleteAccountCommand {
    domain::UserId user_id;
    domain::AccountId account_id;
    int confirmations = 0; // must equal kRequiredConfirmations
};

class DeleteAccountUseCase {
public:
    // Second-confirmation defense count required by the REST contract.
    static constexpr int kRequiredConfirmations = 3;

    DeleteAccountUseCase(
        domain::IAccountRepository& accounts,
        domain::ITransactionRepository& transactions,
        IUnitOfWork& uow)
        : accounts_(accounts), transactions_(transactions), uow_(uow) {}

    [[nodiscard]] VoidResult execute(const DeleteAccountCommand& cmd) {
        if (cmd.confirmations != kRequiredConfirmations) {
            return err(Error::validation(
                "Dangerous action requires " +
                std::to_string(kRequiredConfirmations) + " confirmations"));
        }

        std::optional<Error> app_error;
        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx_ctx) -> domain::RepositoryVoidResult {
                // Lock + ownership check: never delete another user's account,
                // and never leak its existence (find_by_id_for_update maps a
                // foreign/absent account to NotFound).
                auto account =
                    accounts_.find_by_id_for_update(tx_ctx, cmd.account_id, cmd.user_id);
                if (!account) {
                    return std::unexpected(account.error());
                }

                // Purge dependent rows first, then the account itself, all in
                // this transaction so a failure rolls the whole purge back.
                // Transfer aggregates go first as whole units (both legs + the
                // group row) so no dangling half-transfer is left on the other
                // account; then the remaining non-transfer transactions.
                if (auto r = transactions_.physical_delete_transfers_touching_account(
                        tx_ctx, cmd.account_id);
                    !r) {
                    return std::unexpected(r.error());
                }
                if (auto r = transactions_.physical_delete_by_account(tx_ctx, cmd.account_id);
                    !r) {
                    return std::unexpected(r.error());
                }
                if (auto r = accounts_.delete_balance_cache(tx_ctx, cmd.account_id); !r) {
                    return std::unexpected(r.error());
                }
                if (auto r = accounts_.physical_delete(tx_ctx, cmd.account_id); !r) {
                    return std::unexpected(r.error());
                }

                // Event name must match the S11 event contract (14_Event_Design
                // §2.3) so AuditLogHandler / SecurityNotificationHandler receive it.
                uow_.register_event(std::make_shared<domain::SimpleDomainEvent>(
                    "AccountDangerouslyDeleted",
                    "Account",
                    cmd.account_id.to_string(),
                    "{\"user_id\":" + cmd.user_id.to_string() + "}"));
                return {};
            });
        if (!write) {
            if (app_error.has_value()) {
                return err(*app_error);
            }
            return err(from_repository(write.error()));
        }
        return ok();
    }

private:
    domain::IAccountRepository& accounts_;
    domain::ITransactionRepository& transactions_;
    IUnitOfWork& uow_;
};

} // namespace pfh::application
