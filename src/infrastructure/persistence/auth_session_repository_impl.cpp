// Personal Finance Hub - PostgreSQL Authentication Session Repository

#include "pfh/infrastructure/persistence/auth_session_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <string>

namespace pfh::infrastructure {

namespace {

[[nodiscard]] domain::RepositoryResult<application::RefreshTokenRecord>
map_refresh_token(const drogon::orm::Result& result) {
    if (result.empty()) {
        return std::unexpected(domain::RepositoryError::not_found(
            "Refresh token not found"));
    }
    try {
        application::RefreshTokenRecord token;
        token.id = pg::getBigInt(result[0], 0);
        token.user_id = domain::UserId(pg::getBigInt(result[0], 1));
        token.token_hash = pg::getString(result[0], 2);
        token.session_id = pg::getString(result[0], 3);
        token.expires_at = pg::getTimestamp(result[0], 4);
        token.created_at = pg::getTimestamp(result[0], 5);
        token.revoked_at = pg::getOptionalTimestamp(result[0], 6);
        return token;
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored refresh token row is invalid"));
    }
}

constexpr const char* kRefreshSelect = R"SQL(
    SELECT id, user_id, token_hash, session_id,
           expires_at, created_at, revoked_at
    FROM refresh_tokens
    WHERE token_hash = $1
)SQL";

} // namespace

domain::RepositoryResult<application::RefreshTokenRecord>
AuthSessionRepositoryImpl::find_refresh_token(std::string_view token_hash) {
    if (!request_db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Authentication database client is unavailable"));
    }
    try {
        return map_refresh_token(request_db_->execSqlSync(
            kRefreshSelect, std::string(token_hash)));
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "find refresh token", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "find refresh token", error));
    }
}

domain::RepositoryResult<application::RefreshTokenRecord>
AuthSessionRepositoryImpl::find_refresh_token_for_update(
    domain::ITransactionContext& tx_iface,
    std::string_view token_hash) {
    auto context = postgres::require_transaction(tx_iface);
    if (!context) {
        return std::unexpected(context.error());
    }
    try {
        const std::string sql = std::string(kRefreshSelect) + " FOR UPDATE";
        auto token = map_refresh_token((*context)->transaction().execSqlSync(
            sql, std::string(token_hash)));
        if (!token) {
            return token;
        }
        if ((*context)->tenant_user_id() != token->user_id) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Refresh token not found"));
        }
        return token;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "lock refresh token", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "lock refresh token", error));
    }
}

domain::RepositoryVoidResult AuthSessionRepositoryImpl::save_refresh_token(
    domain::ITransactionContext& tx_iface,
    const application::RefreshTokenRecord& token) {
    auto context = postgres::require_transaction(tx_iface, token.user_id);
    if (!context) {
        return std::unexpected(context.error());
    }
    if (token.token_hash.size() != 64 || token.session_id.empty() ||
        token.expires_at <= token.created_at) {
        return std::unexpected(domain::RepositoryError::validation(
            "Invalid refresh token record"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            INSERT INTO refresh_tokens (
                user_id, token_hash, session_id, expires_at, created_at, revoked_at)
            VALUES ($1, $2, $3, $4, $5, $6)
        )SQL";
        (*context)->transaction().execSqlSync(
            kSql,
            token.user_id.value(),
            token.token_hash,
            token.session_id,
            pg::toDbTimestamp(token.expires_at),
            pg::toDbTimestamp(token.created_at),
            pg::toDbTimestamp(token.revoked_at));
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        const std::string detail = error.base().what();
        if (detail.find("23505") != std::string::npos) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Refresh token hash already exists"));
        }
        return std::unexpected(postgres::database_error(
            "save refresh token", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "save refresh token", error));
    }
}

domain::RepositoryVoidResult AuthSessionRepositoryImpl::revoke_refresh_token(
    domain::ITransactionContext& tx_iface,
    std::string_view token_hash,
    application::AuthTimePoint revoked_at) {
    auto context = postgres::require_transaction(tx_iface);
    if (!context || !(*context)->tenant_user_id().has_value()) {
        return std::unexpected(context ? domain::RepositoryError::validation(
            "Refresh revocation requires a tenant") : context.error());
    }
    try {
        constexpr const char* kSql = R"SQL(
            UPDATE refresh_tokens
            SET revoked_at = COALESCE(revoked_at, $1)
            WHERE token_hash = $2 AND user_id = $3
        )SQL";
        const auto result = (*context)->transaction().execSqlSync(
            kSql,
            pg::toDbTimestamp(revoked_at),
            std::string(token_hash),
            (*context)->tenant_user_id()->value());
        if (result.affectedRows() == 0) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Refresh token not found"));
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "revoke refresh token", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "revoke refresh token", error));
    }
}

domain::RepositoryVoidResult AuthSessionRepositoryImpl::revoke_session(
    domain::ITransactionContext& tx_iface,
    domain::UserId user_id,
    std::string_view session_id,
    application::AuthTimePoint revoked_at,
    application::AuthTimePoint expires_at,
    std::string_view reason) {
    auto context = postgres::require_transaction(tx_iface, user_id);
    if (!context) {
        return std::unexpected(context.error());
    }
    if (session_id.empty() || expires_at <= revoked_at) {
        return std::unexpected(domain::RepositoryError::validation(
            "Invalid revoked session record"));
    }
    try {
        constexpr const char* kRevokeTokens = R"SQL(
            UPDATE refresh_tokens
            SET revoked_at = COALESCE(revoked_at, $1)
            WHERE user_id = $2 AND session_id = $3
        )SQL";
        (*context)->transaction().execSqlSync(
            kRevokeTokens,
            pg::toDbTimestamp(revoked_at),
            user_id.value(),
            std::string(session_id));

        constexpr const char* kRevokeSession = R"SQL(
            INSERT INTO revoked_sessions (
                session_id, user_id, expires_at, revoked_at, reason)
            VALUES ($1, $2, $3, $4, $5)
            ON CONFLICT (session_id) DO UPDATE SET
                expires_at = GREATEST(revoked_sessions.expires_at, EXCLUDED.expires_at),
                revoked_at = LEAST(revoked_sessions.revoked_at, EXCLUDED.revoked_at),
                reason = EXCLUDED.reason
        )SQL";
        (*context)->transaction().execSqlSync(
            kRevokeSession,
            std::string(session_id),
            user_id.value(),
            pg::toDbTimestamp(expires_at),
            pg::toDbTimestamp(revoked_at),
            std::string(reason));
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "revoke authentication session", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "revoke authentication session", error));
    }
}

domain::RepositoryVoidResult AuthSessionRepositoryImpl::revoke_access_token(
    domain::ITransactionContext& tx_iface,
    const application::AccessTokenClaims& claims,
    application::AuthTimePoint revoked_at) {
    auto context = postgres::require_transaction(tx_iface, claims.user_id);
    if (!context) {
        return std::unexpected(context.error());
    }
    try {
        constexpr const char* kSql = R"SQL(
            INSERT INTO revoked_access_tokens (
                issuer, jti, session_id, expires_at, revoked_at)
            VALUES ($1, $2, $3, $4, $5)
            ON CONFLICT (issuer, jti) DO NOTHING
        )SQL";
        (*context)->transaction().execSqlSync(
            kSql,
            claims.issuer,
            claims.token_id,
            claims.session_id,
            pg::toDbTimestamp(claims.expires_at),
            pg::toDbTimestamp(revoked_at));
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "revoke access token", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "revoke access token", error));
    }
}

domain::RepositoryResult<bool>
AuthSessionRepositoryImpl::is_access_token_revoked(
    std::string_view issuer,
    std::string_view token_id,
    application::AuthTimePoint now) {
    if (!request_db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Authentication database client is unavailable"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            SELECT 1 FROM revoked_access_tokens
            WHERE issuer = $1 AND jti = $2 AND expires_at > $3
            LIMIT 1
        )SQL";
        return !request_db_->execSqlSync(
            kSql,
            std::string(issuer),
            std::string(token_id),
            pg::toDbTimestamp(now)).empty();
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "check access token revocation", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "check access token revocation", error));
    }
}

domain::RepositoryResult<bool> AuthSessionRepositoryImpl::is_session_revoked(
    std::string_view session_id,
    application::AuthTimePoint now) {
    if (!request_db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Authentication database client is unavailable"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            SELECT 1 FROM revoked_sessions
            WHERE session_id = $1 AND expires_at > $2
            LIMIT 1
        )SQL";
        return !request_db_->execSqlSync(
            kSql,
            std::string(session_id),
            pg::toDbTimestamp(now)).empty();
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "check session revocation", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "check session revocation", error));
    }
}

domain::RepositoryResult<bool> AuthSessionRepositoryImpl::is_session_revoked(
    domain::ITransactionContext& tx_iface,
    std::string_view session_id,
    application::AuthTimePoint now) {
    auto context = postgres::require_transaction(tx_iface);
    if (!context || !(*context)->tenant_user_id().has_value()) {
        return std::unexpected(context ? domain::RepositoryError::validation(
            "Session revocation check requires a tenant") : context.error());
    }
    try {
        constexpr const char* kSql = R"SQL(
            SELECT 1 FROM revoked_sessions
            WHERE session_id = $1
              AND user_id = $2
              AND expires_at > $3
            LIMIT 1
        )SQL";
        return !(*context)->transaction().execSqlSync(
            kSql,
            std::string(session_id),
            (*context)->tenant_user_id()->value(),
            pg::toDbTimestamp(now)).empty();
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "check session revocation in transaction", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "check session revocation in transaction", error));
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
