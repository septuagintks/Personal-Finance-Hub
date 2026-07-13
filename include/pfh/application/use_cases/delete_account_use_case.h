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
#include "pfh/domain/events/domain_events.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include <chrono>
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
        IUnitOfWork& uow,
        domain::IAuditLogRepository& audit_logs)
        : accounts_(accounts),
          transactions_(transactions),
          uow_(uow),
          audit_logs_(audit_logs) {}

    [[nodiscard]] VoidResult execute(const DeleteAccountCommand& cmd) {
        if (!cmd.user_id.is_valid() || !cmd.account_id.is_valid()) {
            return err(Error::validation("User and account ids must be valid"));
        }
        if (cmd.confirmations != kRequiredConfirmations) {
            return err(Error::validation(
                "Dangerous action requires " +
                std::to_string(kRequiredConfirmations) + " confirmations"));
        }

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

                const auto occurred_at = std::chrono::system_clock::now();
                domain::AuditLogEntry audit;
                audit.operator_user_id = cmd.user_id;
                audit.action = domain::AuditAction::DangerousDelete;
                audit.resource_type = "Account";
                audit.resource_id = cmd.account_id.to_string();
                audit.before_value_json = "{\"name\":" +
                    domain::event_detail::json_string(account->name()) + "}";
                audit.after_value_json = "{\"deleted\":true}";
                audit.metadata_json =
                    "{\"confirmations\":" +
                    std::to_string(cmd.confirmations) + "}";
                audit.occurred_at = occurred_at;
                if (auto appended = audit_logs_.append(tx_ctx, audit); !appended) {
                    return appended;
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

                // Strongly-typed event matching the S11 contract (14_Event_Design
                // section 2.3). Business audit is already committed above;
                // post-commit handlers use this for cache/security side effects.
                uow_.register_event(std::make_shared<domain::AccountDangerouslyDeletedEvent>(
                    cmd.user_id,
                    cmd.account_id,
                    occurred_at));
                return {};
            });
        if (!write) {
            return err(from_repository(write.error()));
        }
        return ok();
    }

private:
    domain::IAccountRepository& accounts_;
    domain::ITransactionRepository& transactions_;
    IUnitOfWork& uow_;
    domain::IAuditLogRepository& audit_logs_;
};

} // namespace pfh::application
