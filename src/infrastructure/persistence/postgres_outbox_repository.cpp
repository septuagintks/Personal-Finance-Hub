// Personal Finance Hub - PostgreSQL Transactional Outbox Repository

#include "pfh/infrastructure/persistence/postgres_outbox_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace pfh::infrastructure {

namespace {

[[nodiscard]] application::OutboxStatus parse_status(
    std::string_view status) {
    if (status == "pending") return application::OutboxStatus::Pending;
    if (status == "processing") return application::OutboxStatus::Processing;
    if (status == "published") return application::OutboxStatus::Published;
    if (status == "failed") return application::OutboxStatus::Failed;
    if (status == "dead_letter") return application::OutboxStatus::DeadLetter;
    throw std::invalid_argument("unknown outbox status");
}

[[nodiscard]] application::OutboxMessage map_message(
    const drogon::orm::Row& row) {
    application::OutboxMessage message;
    message.id = pg::getString(row, 0);
    message.event_name = pg::getString(row, 1);
    message.aggregate_type = pg::getOptionalString(row, 2).value_or("");
    message.aggregate_id = pg::getOptionalString(row, 3).value_or("");
    message.payload_json = pg::getString(row, 4);
    message.status = parse_status(pg::getString(row, 5));
    message.retry_count = row[6].as<int>();
    message.max_retry_count = row[7].as<int>();
    message.next_retry_at = pg::getTimestamp(row, 8);
    message.last_error = pg::getOptionalString(row, 9).value_or("");
    message.last_failed_handler = pg::getOptionalString(row, 10).value_or("");
    message.last_failed_at = pg::getOptionalTimestamp(row, 11).value_or(
        std::chrono::system_clock::time_point{});
    message.occurred_at = pg::getTimestamp(row, 12);
    message.created_at = pg::getTimestamp(row, 13);
    message.published_at = pg::getOptionalTimestamp(row, 14).value_or(
        std::chrono::system_clock::time_point{});
    message.locked_at = pg::getOptionalTimestamp(row, 15).value_or(
        std::chrono::system_clock::time_point{});
    message.locked_by = pg::getOptionalString(row, 16).value_or("");
    message.claim_token = pg::getOptionalString(row, 17).value_or("");
    if (message.id.empty() || message.event_name.empty() ||
        message.retry_count < 0 || message.max_retry_count <= 0) {
        throw std::invalid_argument("invalid outbox row");
    }
    return message;
}

constexpr std::string_view kReturningColumns = R"SQL(
    event.id::text, event.event_name, event.aggregate_type,
    event.aggregate_id, event.payload::text, event.status::text,
    event.retry_count, event.max_retry_count, event.next_retry_at,
    event.last_error, event.last_failed_handler, event.last_failed_at,
    event.occurred_at,
    event.created_at, event.published_at, event.locked_at, event.locked_by,
    event.claim_token::text
)SQL";

[[nodiscard]] std::int64_t checked_limit(std::size_t value) {
    if (value == 0 ||
        value > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("outbox batch size is invalid");
    }
    return static_cast<std::int64_t>(value);
}

} // namespace

domain::RepositoryResult<application::OutboxClaimBatch>
PostgresOutboxRepository::claim_due(
    std::chrono::system_clock::time_point now,
    std::chrono::seconds processing_timeout,
    std::size_t batch_size,
    std::string_view worker_id) {
    if (!db_ || processing_timeout <= std::chrono::seconds::zero() ||
        worker_id.empty() || worker_id.size() > 128) {
        return std::unexpected(domain::RepositoryError::validation(
            "Invalid outbox claim request"));
    }

    // Lease ownership is shared across hosts, so PostgreSQL time is the
    // authority for due and stale decisions. In-memory adapters use `now` for
    // deterministic tests.
    (void)now;
    try {
        const auto limit = checked_limit(batch_size);
        return postgres::execute_transaction<application::OutboxClaimBatch>(
            db_,
            std::nullopt,
            "claim outbox events",
            [&](const std::shared_ptr<drogon::orm::Transaction>& tx)
                -> domain::RepositoryResult<application::OutboxClaimBatch> {
                application::OutboxClaimBatch batch;
                const std::string recover_sql = R"SQL(
                    WITH stale AS (
                        SELECT id
                        FROM domain_events_outbox
                        WHERE status = 'processing'::outbox_status
                          AND locked_at <=
                              NOW() - ($1::bigint * INTERVAL '1 second')
                        ORDER BY locked_at, created_at, id
                        LIMIT $2
                        FOR UPDATE SKIP LOCKED
                    )
                    UPDATE domain_events_outbox AS event
                    SET retry_count = LEAST(
                            event.retry_count + 1,
                            event.max_retry_count),
                        status = CASE
                            WHEN event.retry_count + 1 >= event.max_retry_count
                                THEN 'dead_letter'::outbox_status
                            ELSE 'failed'::outbox_status
                        END,
                        next_retry_at = NOW(),
                        last_error = 'processing lease expired',
                        last_failed_handler = 'outbox-lease',
                        last_failed_at = NOW(),
                        locked_at = NULL,
                        locked_by = NULL,
                        claim_token = NULL
                    FROM stale
                    WHERE event.id = stale.id
                    RETURNING )SQL" + std::string(kReturningColumns);
                const auto recovered = tx->execSqlSync(
                    recover_sql,
                    processing_timeout.count(),
                    limit);
                for (const auto& row : recovered) {
                    auto message = map_message(row);
                    if (message.status == application::OutboxStatus::DeadLetter) {
                        batch.recovered_dead_letters.push_back(
                            std::move(message));
                    }
                }

                const std::string claim_sql = R"SQL(
                    WITH due AS (
                        SELECT id
                        FROM domain_events_outbox
                        WHERE status IN (
                                'pending'::outbox_status,
                                'failed'::outbox_status)
                          AND next_retry_at <= NOW()
                        ORDER BY created_at, id
                        LIMIT $1
                        FOR UPDATE SKIP LOCKED
                    )
                    UPDATE domain_events_outbox AS event
                    SET status = 'processing'::outbox_status,
                        locked_at = NOW(),
                        locked_by = $2,
                        claim_token = gen_random_uuid()
                    FROM due
                    WHERE event.id = due.id
                    RETURNING )SQL" + std::string(kReturningColumns);
                const auto claimed = tx->execSqlSync(
                    claim_sql,
                    limit,
                    std::string(worker_id));
                batch.claimed.reserve(claimed.size());
                for (const auto& row : claimed) {
                    batch.claimed.push_back(map_message(row));
                }
                return batch;
            });
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "claim outbox events", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "claim outbox events", error));
    }
}

domain::RepositoryVoidResult PostgresOutboxRepository::mark_published(
    std::string_view outbox_id,
    std::string_view claim_token,
    std::chrono::system_clock::time_point published_at) {
    if (!db_ || outbox_id.empty() || claim_token.empty()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Invalid outbox publish transition"));
    }
    (void)published_at;
    try {
        constexpr const char* kSql = R"SQL(
            UPDATE domain_events_outbox
            SET status = 'published'::outbox_status,
                published_at = NOW(),
                locked_at = NULL,
                locked_by = NULL,
                claim_token = NULL
            WHERE id = $1::uuid
              AND status = 'processing'::outbox_status
              AND claim_token = $2::uuid
        )SQL";
        const auto result = db_->execSqlSync(
            kSql,
            std::string(outbox_id),
            std::string(claim_token));
        if (result.affectedRows() != 1) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Outbox claim is no longer owned"));
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "mark outbox published", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "mark outbox published", error));
    }
}

domain::RepositoryResult<application::OutboxFailureTransition>
PostgresOutboxRepository::mark_failed(
    std::string_view outbox_id,
    std::string_view claim_token,
    std::string_view handler_name,
    std::string_view error_summary,
    std::chrono::system_clock::time_point failed_at,
    std::chrono::system_clock::time_point next_retry_at) {
    if (!db_ || outbox_id.empty() || claim_token.empty() ||
        handler_name.empty() || handler_name.size() > 128 ||
        error_summary.empty() || error_summary.size() > 1024 ||
        next_retry_at <= failed_at) {
        return std::unexpected(domain::RepositoryError::validation(
            "Invalid outbox failure transition"));
    }
    const auto retry_delay = std::chrono::duration_cast<std::chrono::seconds>(
        next_retry_at - failed_at);
    if (retry_delay <= std::chrono::seconds::zero()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Invalid outbox retry delay"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            UPDATE domain_events_outbox
            SET retry_count = LEAST(retry_count + 1, max_retry_count),
                status = CASE
                    WHEN retry_count + 1 >= max_retry_count
                        THEN 'dead_letter'::outbox_status
                    ELSE 'failed'::outbox_status
                END,
                next_retry_at =
                    NOW() + ($5::bigint * INTERVAL '1 second'),
                last_error = $4,
                last_failed_handler = $3,
                last_failed_at = NOW(),
                locked_at = NULL,
                locked_by = NULL,
                claim_token = NULL
            WHERE id = $1::uuid
              AND status = 'processing'::outbox_status
              AND claim_token = $2::uuid
            RETURNING status::text, retry_count
        )SQL";
        const auto result = db_->execSqlSync(
            kSql,
            std::string(outbox_id),
            std::string(claim_token),
            std::string(handler_name),
            std::string(error_summary),
            retry_delay.count());
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Outbox claim is no longer owned"));
        }
        application::OutboxFailureTransition transition;
        transition.disposition =
            parse_status(pg::getString(result[0], 0)) ==
                    application::OutboxStatus::DeadLetter
                ? application::OutboxFailureDisposition::DeadLettered
                : application::OutboxFailureDisposition::RetryScheduled;
        transition.retry_count = result[0][1].as<int>();
        return transition;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "mark outbox failed", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "mark outbox failed", error));
    }
}

domain::RepositoryResult<std::vector<application::OutboxMessage>>
PostgresOutboxRepository::list_unhandled_dead_letters(
    std::string_view handler_name,
    std::size_t limit) {
    if (!db_ || handler_name.empty() || handler_name.size() > 128) {
        return std::unexpected(domain::RepositoryError::validation(
            "Invalid dead-letter audit query"));
    }
    try {
        const auto checked = checked_limit(limit);
        const std::string sql = R"SQL(
            SELECT )SQL" + std::string(kReturningColumns) + R"SQL(
            FROM domain_events_outbox AS event
            WHERE event.status = 'dead_letter'::outbox_status
              AND NOT EXISTS (
                  SELECT 1
                  FROM outbox_handler_receipts AS receipt
                  WHERE receipt.outbox_id = event.id
                    AND receipt.handler_name = $1)
            ORDER BY event.created_at, event.id
            LIMIT $2
        )SQL";
        const auto rows = db_->execSqlSync(
            sql, std::string(handler_name), checked);
        std::vector<application::OutboxMessage> messages;
        messages.reserve(rows.size());
        for (const auto& row : rows) {
            messages.push_back(map_message(row));
        }
        return messages;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "list unaudited dead letters", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "list unaudited dead letters", error));
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
