// Personal Finance Hub - PostgreSQL Transaction Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_transaction_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>
#include <utility>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for ITransactionRepository.
///
/// Instances are request-scoped. Reads bind tenant_user_id in a short pinned
/// transaction; writes additionally verify the supplied UnitOfWork context.
class TransactionRepositoryImpl final : public domain::ITransactionRepository {
public:
    TransactionRepositoryImpl(
        drogon::orm::DbClientPtr db,
        domain::UserId tenant_user_id)
        : db_(std::move(db)), tenant_user_id_(tenant_user_id) {}

    [[nodiscard]] domain::RepositoryResult<domain::Transaction> find_by_id(
        domain::TransactionId id) override;

    [[nodiscard]] domain::RepositoryResult<domain::TransactionReadModel> find_detail(
        domain::TransactionId id,
        domain::UserId user_id,
        bool include_deleted = true) override;

    [[nodiscard]] domain::RepositoryResult<domain::TransactionPageResult> find_page(
        const domain::TransactionPageQuery& query) override;

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

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Transaction>>
    find_by_user_in_range(
        domain::UserId user_id,
        std::optional<std::chrono::system_clock::time_point> from,
        std::optional<std::chrono::system_clock::time_point> to,
        bool include_deleted = false) override;

    [[nodiscard]] domain::RepositoryResult<domain::TransferSnapshot>
    find_transfer_by_group(
        domain::TransferGroupId group_id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryVoidResult soft_delete(
        domain::ITransactionContext& tx,
        domain::TransactionId id,
        domain::UserId user_id,
        std::chrono::system_clock::time_point deleted_at) override;

    [[nodiscard]] domain::RepositoryResult<
        domain::TransactionCorrectionPersistResult> save_correction(
        domain::ITransactionContext& tx,
        domain::TransactionId original_id,
        const domain::Transaction& replacement,
        std::chrono::system_clock::time_point corrected_at) override;

    [[nodiscard]] domain::RepositoryVoidResult physical_delete_by_account(
        domain::ITransactionContext& tx,
        domain::AccountId account_id) override;

    [[nodiscard]] domain::RepositoryVoidResult physical_delete_transfers_touching_account(
        domain::ITransactionContext& tx,
        domain::AccountId account_id) override;

private:
    drogon::orm::DbClientPtr db_;
    domain::UserId tenant_user_id_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
