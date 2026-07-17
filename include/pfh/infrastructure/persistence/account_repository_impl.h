// Personal Finance Hub - PostgreSQL Account Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_account_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>
#include <utility>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for IAccountRepository.
///
/// Instances are request-scoped. Every read pins a short transaction and binds
/// tenant_user_id with SET LOCAL before touching an RLS table.
class AccountRepositoryImpl final : public domain::IAccountRepository {
public:
    AccountRepositoryImpl(
        drogon::orm::DbClientPtr db,
        domain::UserId tenant_user_id)
        : db_(std::move(db)), tenant_user_id_(tenant_user_id) {}

    [[nodiscard]] domain::RepositoryResult<domain::Account> find_by_id(
        domain::AccountId id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Account> find_by_id_for_user(
        domain::AccountId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Account> find_by_id_for_update(
        domain::ITransactionContext& tx,
        domain::AccountId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Account>> find_active_by_user(
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Account>> find_by_user(
        domain::UserId user_id,
        std::optional<bool> archived) override;

    [[nodiscard]] domain::RepositoryResult<bool> has_transactions(
        domain::ITransactionContext& tx,
        domain::AccountId id) override;

    [[nodiscard]] domain::RepositoryResult<domain::BalanceSnapshot> balance_of(
        domain::AccountId id) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::AccountBalanceAt>>
    balances_at(
        domain::UserId user_id,
        std::chrono::system_clock::time_point as_of) override;

    [[nodiscard]] domain::RepositoryResult<domain::AccountId> save(
        domain::ITransactionContext& tx,
        const domain::Account& account) override;

    [[nodiscard]] domain::RepositoryVoidResult delete_balance_cache(
        domain::ITransactionContext& tx,
        domain::AccountId id) override;

    [[nodiscard]] domain::RepositoryVoidResult physical_delete(
        domain::ITransactionContext& tx,
        domain::AccountId id) override;

private:
    drogon::orm::DbClientPtr db_;
    domain::UserId tenant_user_id_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
