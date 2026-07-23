// Personal Finance Hub - PostgreSQL Published Outbox Retention

#include "pfh/infrastructure/persistence/postgres_outbox_retention_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

namespace pfh::infrastructure {

domain::RepositoryResult<std::size_t>
PostgresOutboxRetentionRepository::cleanup_published(
    std::chrono::system_clock::time_point now,
    std::chrono::seconds retention,
    std::size_t batch_size) {
    if (!db_ || retention < std::chrono::hours(24) ||
        retention > std::chrono::hours(24 * 3650) ||
        batch_size == 0 || batch_size > 10000 ||
        batch_size > static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max())) {
        return std::unexpected(domain::RepositoryError::validation(
            "Outbox retention request is invalid"));
    }

    // PostgreSQL time is authoritative across scheduler instances. The clock
    // argument remains part of the port for deterministic in-memory tests.
    (void)now;
    const auto limit = static_cast<std::int64_t>(batch_size);
    return postgres::execute_transaction<std::size_t>(
        db_,
        std::nullopt,
        "cleanup published outbox",
        [&](const std::shared_ptr<drogon::orm::Transaction>& transaction)
            -> domain::RepositoryResult<std::size_t> {
            const auto candidates = transaction->execSqlSync(R"SQL(
                SELECT id::text
                FROM domain_events_outbox
                WHERE status = 'published'::outbox_status
                  AND created_at <
                      NOW() - ($1::bigint * INTERVAL '1 second')
                  AND published_at <
                      NOW() - ($1::bigint * INTERVAL '1 second')
                ORDER BY created_at, id
                LIMIT $2
                FOR UPDATE SKIP LOCKED
            )SQL", retention.count(), limit);
            if (candidates.empty()) return std::size_t{0};

            nlohmann::json ids = nlohmann::json::array();
            for (const auto& row : candidates) {
                ids.push_back(pg::getString(row, 0));
            }
            const auto encoded_ids = ids.dump();

            // Retry facts have a restrictive foreign key. Remove them before
            // deleting their locked event rows; receipts cascade from Outbox.
            transaction->execSqlSync(R"SQL(
                DELETE FROM outbox_retry_commands
                WHERE outbox_id IN (
                    SELECT value::uuid
                    FROM jsonb_array_elements_text($1::jsonb))
            )SQL", encoded_ids);
            const auto deleted = transaction->execSqlSync(R"SQL(
                DELETE FROM domain_events_outbox
                WHERE id IN (
                    SELECT value::uuid
                    FROM jsonb_array_elements_text($1::jsonb))
                  AND status = 'published'::outbox_status
                RETURNING id
            )SQL", encoded_ids);
            if (deleted.size() != candidates.size()) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Outbox retention candidate changed while locked"));
            }
            return deleted.size();
        });
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
