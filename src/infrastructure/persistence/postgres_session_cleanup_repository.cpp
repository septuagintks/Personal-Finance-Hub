// Personal Finance Hub - PostgreSQL Authentication Data Cleanup

#include "pfh/infrastructure/persistence/postgres_session_cleanup_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <cstdint>
#include <limits>
#include <optional>

namespace pfh::infrastructure {

domain::RepositoryResult<application::SessionCleanupSummary>
PostgresSessionCleanupRepository::delete_expired(
    std::chrono::system_clock::time_point now,
    std::size_t batch_size) {
    if (!db_ || batch_size == 0 ||
        batch_size >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::unexpected(domain::RepositoryError::validation(
            "Session cleanup request is invalid"));
    }
    const auto limit = static_cast<std::int64_t>(batch_size);
    // Revocation rows are security controls. Use the database clock so a host
    // clock running ahead cannot delete them before their real expiry.
    (void)now;
    return postgres::execute_transaction<application::SessionCleanupSummary>(
        db_,
        std::nullopt,
        "clean expired authentication data",
        [&](const std::shared_ptr<drogon::orm::Transaction>& transaction)
            -> domain::RepositoryResult<application::SessionCleanupSummary> {
            constexpr const char* kRefreshSql = R"SQL(
                WITH expired AS (
                    SELECT id
                    FROM refresh_tokens
                    WHERE expires_at <= NOW()
                    ORDER BY expires_at, id
                    LIMIT $1
                    FOR UPDATE SKIP LOCKED
                )
                DELETE FROM refresh_tokens AS token
                USING expired
                WHERE token.id = expired.id
                RETURNING token.id
            )SQL";
            constexpr const char* kAccessSql = R"SQL(
                WITH expired AS (
                    SELECT issuer, jti
                    FROM revoked_access_tokens
                    WHERE expires_at <= NOW()
                    ORDER BY expires_at, issuer, jti
                    LIMIT $1
                    FOR UPDATE SKIP LOCKED
                )
                DELETE FROM revoked_access_tokens AS token
                USING expired
                WHERE token.issuer = expired.issuer
                  AND token.jti = expired.jti
                RETURNING token.jti
            )SQL";
            constexpr const char* kSessionSql = R"SQL(
                WITH expired AS (
                    SELECT session_id
                    FROM revoked_sessions
                    WHERE expires_at <= NOW()
                    ORDER BY expires_at, session_id
                    LIMIT $1
                    FOR UPDATE SKIP LOCKED
                )
                DELETE FROM revoked_sessions AS revoked
                USING expired
                WHERE revoked.session_id = expired.session_id
                RETURNING revoked.session_id
            )SQL";
            application::SessionCleanupSummary summary;
            summary.refresh_tokens_deleted = transaction->execSqlSync(
                kRefreshSql, limit).size();
            summary.revoked_access_tokens_deleted = transaction->execSqlSync(
                kAccessSql, limit).size();
            summary.revoked_sessions_deleted = transaction->execSqlSync(
                kSessionSql, limit).size();
            return summary;
        });
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
