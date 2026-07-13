// Personal Finance Hub - PostgreSQL Transaction Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_transaction_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for ITransactionRepository.
///
/// `transactions` IS RLS-scoped. All writes go through an active transaction
/// with the GUC set. Transfer aggregates are atomic: transfer_groups row +
/// outgoing + incoming (+ adjustments) in one DB transaction.
class TransactionRepositoryImpl final : public domain::ITransactionRepository {
public:
    explicit TransactionRepositoryImpl(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<domain::Transaction> find_by_id(
        domain::TransactionId id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Transaction> find_by_id_for_update(
        domain::ITransactionContext& tx,
        domain::TransactionId id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Transaction> save_single(
        domain::ITransactionContext& tx,
        const domain::Transaction& transaction) override;

    [[nodiscard]] domain::RepositoryResult<domain::TransferPersistResult> save_transfer(
        domain::ITransactionContext& tx,
        const domain::TransferAggregate& transfer) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Transaction>> find_by_account(
        domain::AccountId account_id,
        std::optional<std::chrono::system_clock::time_point> from = std::nullopt,
        std::optional<std::chrono::system_clock::time_point> to = std::nullopt,
        bool include_deleted = false) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Transaction>> find_by_user(
        domain::UserId user_id,
        bool include_deleted = false) override;

    [[nodiscard]] domain::RepositoryVoidResult soft_delete(
        domain::ITransactionContext& tx,
        domain::TransactionId id,
        domain::UserId user_id,
        std::chrono::system_clock::time_point deleted_at) override;

    [[nodiscard]] domain::RepositoryVoidResult physical_delete_by_account(
        domain::ITransactionContext& tx,
        domain::AccountId account_id) override;

    [[nodiscard]] domain::RepositoryVoidResult physical_delete_transfers_touching_account(
        domain::ITransactionContext& tx,
        domain::AccountId account_id) override;

private:
    drogon::orm::DbClientPtr db_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL