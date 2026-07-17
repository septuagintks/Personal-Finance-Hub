// Personal Finance Hub - PostgreSQL Idempotency Cleanup Adapter

#include "pfh/infrastructure/persistence/postgres_idempotency_cleanup_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"

#include <cstdint>
#include <exception>
#include <limits>

namespace pfh::infrastructure {

domain::RepositoryResult<std::size_t>
PostgresIdempotencyCleanupRepository::cleanup_expired(
    std::chrono::system_clock::time_point now,
    std::size_t batch_size) {
    if (!db_ || batch_size == 0 || batch_size > 10000 ||
        batch_size > static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max())) {
        return std::unexpected(domain::RepositoryError::validation(
            "Idempotency cleanup batch is invalid"));
    }
    (void)now;
    try {
        const auto rows = db_->execSqlSync(
            "SELECT pfh_cleanup_expired_request_idempotency($1)",
            static_cast<std::int32_t>(batch_size));
        if (rows.size() != 1) {
            return std::unexpected(domain::RepositoryError::database(
                "Idempotency cleanup returned no result"));
        }
        const auto deleted = rows[0][0].as<std::int32_t>();
        if (deleted < 0) {
            return std::unexpected(domain::RepositoryError::database(
                "Idempotency cleanup returned an invalid count"));
        }
        return static_cast<std::size_t>(deleted);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "cleanup request idempotency", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "cleanup request idempotency", error));
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
