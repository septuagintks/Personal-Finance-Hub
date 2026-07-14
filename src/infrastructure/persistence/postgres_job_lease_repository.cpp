// Personal Finance Hub - PostgreSQL Scheduled Job Lease Repository

#include "pfh/infrastructure/persistence/postgres_job_lease_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <exception>
#include <string>

namespace pfh::infrastructure {

namespace {

[[nodiscard]] bool valid_identity(std::string_view value) {
    return !value.empty() && value.size() <= 128;
}

} // namespace

domain::RepositoryResult<std::optional<application::JobLease>>
PostgresJobLeaseRepository::try_acquire(
    std::string_view job_name,
    std::string_view owner_id,
    std::chrono::system_clock::time_point now,
    std::chrono::seconds lease_duration) {
    if (!db_ || !valid_identity(job_name) || !valid_identity(owner_id) ||
        lease_duration <= std::chrono::seconds::zero()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Scheduled job lease request is invalid"));
    }
    // The database clock is the shared authority across scheduler instances;
    // the injected clock remains useful for deterministic in-memory tests.
    (void)now;
    try {
        constexpr const char* kSql = R"SQL(
            INSERT INTO scheduled_job_leases AS lease (
                job_name, owner_id, lease_token, lease_until, updated_at)
            VALUES (
                $1, $2, gen_random_uuid(),
                NOW() + ($3::bigint * INTERVAL '1 second'), NOW())
            ON CONFLICT (job_name) DO UPDATE
            SET owner_id = EXCLUDED.owner_id,
                lease_token = gen_random_uuid(),
                lease_until = EXCLUDED.lease_until,
                updated_at = EXCLUDED.updated_at
            WHERE lease.lease_until <= NOW()
            RETURNING lease_token::text, lease_until
        )SQL";
        const auto result = db_->execSqlSync(
            kSql,
            std::string(job_name),
            std::string(owner_id),
            lease_duration.count());
        if (result.empty()) {
            return std::optional<application::JobLease>{};
        }
        return std::optional<application::JobLease>(application::JobLease{
            pg::getString(result[0], 0),
            pg::getTimestamp(result[0], 1)});
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "acquire scheduled job lease", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "acquire scheduled job lease", error));
    }
}

domain::RepositoryResult<bool> PostgresJobLeaseRepository::release(
    std::string_view job_name,
    std::string_view owner_id,
    std::string_view lease_token,
    std::chrono::system_clock::time_point released_at) {
    if (!db_ || !valid_identity(job_name) || !valid_identity(owner_id) ||
        lease_token.empty()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Scheduled job lease release is invalid"));
    }
    (void)released_at;
    try {
        constexpr const char* kSql = R"SQL(
            UPDATE scheduled_job_leases
            SET lease_until = NOW(),
                updated_at = NOW()
            WHERE job_name = $1
              AND owner_id = $2
              AND lease_token = $3::uuid
            RETURNING job_name
        )SQL";
        const auto result = db_->execSqlSync(
            kSql,
            std::string(job_name),
            std::string(owner_id),
            std::string(lease_token));
        return !result.empty();
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "release scheduled job lease", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "release scheduled job lease", error));
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
