// Personal Finance Hub - PostgreSQL User Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/application/ports/i_user_credential_reader.h"
#include "pfh/application/ports/i_user_role_reader.h"
#include "pfh/domain/repositories/i_user_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>
#include <utility>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for IUserRepository.
///
/// Note: `users` is intentionally NOT covered by RLS policies in V1 (login must
/// resolve username before a tenant id is known). Application-layer filtering
/// is the guard for direct reads. Writes add an adapter-level boundary: create
/// requires an unscoped registration transaction, while save requires a
/// transaction bound to the same user id.
class UserRepositoryImpl final
    : public domain::IUserRepository,
      public application::IUserCredentialReader,
      public application::IUserRoleReader {
public:
    explicit UserRepositoryImpl(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_id(
        domain::UserId id) override;

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_username(
        const std::string& username) override;

    [[nodiscard]] domain::RepositoryResult<application::UserCredentialRecord>
    find_credentials_by_username(const std::string& normalized_username) override;

    [[nodiscard]] domain::RepositoryResult<domain::UserRole> find_role_by_id(
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_id(
        domain::ITransactionContext& tx,
        domain::UserId id) override;

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_username(
        domain::ITransactionContext& tx,
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
