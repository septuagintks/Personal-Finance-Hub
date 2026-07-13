// Personal Finance Hub - Authentication Session Repository Port

#pragma once

#include "pfh/application/security/auth_models.h"
#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"

#include <string>
#include <string_view>

namespace pfh::application {

class IAuthSessionRepository {
public:
    virtual ~IAuthSessionRepository() = default;

    [[nodiscard]] virtual domain::RepositoryResult<RefreshTokenRecord>
    find_refresh_token(std::string_view token_hash) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<RefreshTokenRecord>
    find_refresh_token_for_update(
        domain::ITransactionContext& tx,
        std::string_view token_hash) = 0;

    [[nodiscard]] virtual domain::RepositoryVoidResult save_refresh_token(
        domain::ITransactionContext& tx,
        const RefreshTokenRecord& token) = 0;

    [[nodiscard]] virtual domain::RepositoryVoidResult revoke_refresh_token(
        domain::ITransactionContext& tx,
        std::string_view token_hash,
        AuthTimePoint revoked_at) = 0;

    [[nodiscard]] virtual domain::RepositoryVoidResult revoke_session(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        std::string_view session_id,
        AuthTimePoint revoked_at,
        AuthTimePoint expires_at,
        std::string_view reason) = 0;

    [[nodiscard]] virtual domain::RepositoryVoidResult revoke_access_token(
        domain::ITransactionContext& tx,
        const AccessTokenClaims& claims,
        AuthTimePoint revoked_at) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<bool> is_access_token_revoked(
        std::string_view issuer,
        std::string_view token_id,
        AuthTimePoint now) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<bool> is_session_revoked(
        std::string_view session_id,
        AuthTimePoint now) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<bool> is_session_revoked(
        domain::ITransactionContext& tx,
        std::string_view session_id,
        AuthTimePoint now) = 0;
};

} // namespace pfh::application
