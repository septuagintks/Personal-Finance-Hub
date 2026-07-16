// Personal Finance Hub - PostgreSQL Authentication Session Repository

#pragma once

#include "pfh/application/ports/i_auth_session_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class AuthSessionRepositoryImpl final
    : public application::IAuthSessionRepository {
public:
    explicit AuthSessionRepositoryImpl(drogon::orm::DbClientPtr request_db)
        : request_db_(std::move(request_db)) {}

    [[nodiscard]] domain::RepositoryResult<application::RefreshTokenRecord>
    find_refresh_token(std::string_view token_hash) override;

    [[nodiscard]] domain::RepositoryResult<application::RefreshTokenRecord>
    find_refresh_token_for_update(
        domain::ITransactionContext& tx,
        std::string_view token_hash) override;

    [[nodiscard]] domain::RepositoryVoidResult save_refresh_token(
        domain::ITransactionContext& tx,
        const application::RefreshTokenRecord& token) override;

    [[nodiscard]] domain::RepositoryVoidResult revoke_refresh_token(
        domain::ITransactionContext& tx,
        std::string_view token_hash,
        application::AuthTimePoint revoked_at) override;

    [[nodiscard]] domain::RepositoryVoidResult revoke_session(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        std::string_view session_id,
        application::AuthTimePoint revoked_at,
        application::AuthTimePoint expires_at,
        std::string_view reason) override;

    [[nodiscard]] domain::RepositoryVoidResult revoke_access_token(
        domain::ITransactionContext& tx,
        const application::AccessTokenClaims& claims,
        application::AuthTimePoint revoked_at) override;

    [[nodiscard]] domain::RepositoryResult<bool> is_access_token_revoked(
        std::string_view issuer,
        std::string_view token_id,
        application::AuthTimePoint now) override;

    [[nodiscard]] domain::RepositoryResult<bool> is_access_or_session_revoked(
        std::string_view issuer,
        std::string_view token_id,
        std::string_view session_id,
        application::AuthTimePoint now) override;

    [[nodiscard]] domain::RepositoryResult<bool> is_session_revoked(
        std::string_view session_id,
        application::AuthTimePoint now) override;

    [[nodiscard]] domain::RepositoryResult<bool> is_session_revoked(
        domain::ITransactionContext& tx,
        std::string_view session_id,
        application::AuthTimePoint now) override;

private:
    drogon::orm::DbClientPtr request_db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
