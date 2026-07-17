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
#include "pfh/application/transfer_command_builder.h"
#include "pfh/application/transfer_dto_mapper.h"
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
        if (auto shape = validate_transfer_command_shape(cmd); !shape) {
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
                            const auto legacy_group = idempotency_value(
                                started->response_values,
                                "transfer_group_id");
                            const auto legacy_group_value = legacy_group.has_value()
                                ? parse_idempotency_integer(*legacy_group)
                                : std::nullopt;
                            if (!legacy_group_value.has_value() ||
                                *legacy_group_value <= 0) {
                                app_error = restored.error();
                                return std::unexpected(
                                    domain::RepositoryError::database(
                                        "Stored idempotency response is invalid"));
                            }
                            auto snapshot =
                                transactions_.find_transfer_by_group_for_update(
                                    tx_ctx,
                                    domain::TransferGroupId(*legacy_group_value),
                                    cmd.user_id);
                            if (!snapshot) {
                                return std::unexpected(snapshot.error());
                            }
                            restored = to_transfer_dto(*snapshot);
                            if (!restored) {
                                app_error = restored.error();
                                return std::unexpected(
                                    domain::RepositoryError::database(
                                        "Legacy transfer response cannot be rebuilt"));
                            }
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

                auto aggregate_result = build_transfer_aggregate(
                    cmd, *source, *target, third_party_fee_account,
                    std::chrono::system_clock::now());
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
        return to_transfer_dto(persisted, aggregate);
    }

    [[nodiscard]] static IdempotencyValues to_idempotency_values(
        const TransferResultDto& value) {
        return transfer_to_idempotency_values(value);
    }

    [[nodiscard]] static Result<TransferResultDto> from_idempotency_values(
        const IdempotencyValues& values) {
        return transfer_from_idempotency_values(values);
    }

    domain::IAccountRepository& accounts_;
    domain::ITransactionRepository& transactions_;
    IUnitOfWork& uow_;
    IIdempotencyRepository* idempotency_;
};

} // namespace pfh::application
