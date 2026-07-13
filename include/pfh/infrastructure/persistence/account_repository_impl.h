// Personal Finance Hub - PostgreSQL Account Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_account_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for IAccountRepository.
///
/// `accounts` IS RLS-scoped (V1 §RLS). Reads on a pooled connection require
/// `app.current_user_id` to be set; read paths that happen before
/// authentication (none in production, but unit/integration tests exercise
/// this) bind GUC explicitly via RlsSession.
class AccountRepositoryImpl final : public domain::IAccountRepository {
public:
    explicit AccountRepositoryImpl(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

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

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Currency>> find_active_currencies() override;

    [[nodiscard]] domain::RepositoryResult<domain::BalanceSnapshot> balance_of(
        domain::AccountId id) override;

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
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL