// Personal Finance Hub - PostgreSQL User Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_user_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for IUserRepository.
///
/// Note: `users` is intentionally NOT covered by RLS policies in V1 (login must
/// resolve username before a tenant id is known). Application-layer filtering
/// (username/password_hash) is the only guard for direct `users` access, so all
/// writes still go through repositories that are bound to authenticated
/// requests via the use case layer.
class UserRepositoryImpl final : public domain::IUserRepository {
public:
    explicit UserRepositoryImpl(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_id(
        domain::UserId id) override;

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_username(
        const std::string& username) override;

    [[nodiscard]] domain::RepositoryResult<domain::UserId> create(
        domain::ITransactionContext& tx,
        const std::string& username,
        const std::string& password_hash,
        const domain::Currency& base_currency) override;

    [[nodiscard]] domain::RepositoryVoidResult save(
        domain::ITransactionContext& tx,
        const domain::User& user) override;

private:
    drogon::orm::DbClientPtr db_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
