// Personal Finance Hub - PostgreSQL Operations Adapter

#include "pfh/infrastructure/persistence/postgres_operations_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace pfh::infrastructure {

namespace {

[[nodiscard]] bool is_uuid(std::string_view value) noexcept {
    if (value.size() != 36) return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') return false;
        } else if (std::isxdigit(
                       static_cast<unsigned char>(value[index])) == 0) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::int64_t checked_limit(std::size_t limit) {
    if (limit == 0 || limit > 100 ||
        limit >= static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("operations page size is invalid");
    }
    return static_cast<std::int64_t>(limit + 1);
}

} // namespace

domain::RepositoryResult<application::ReadinessState>
PostgresOperationsRepository::readiness(
    std::int64_t expected_migration_version) {
    if (!db_ || expected_migration_version <= 0) {
        return std::unexpected(domain::RepositoryError::validation(
            "Readiness request is invalid"));
    }
    try {
        const auto ping = db_->execSqlSync("SELECT 1");
        if (ping.size() != 1) {
            return application::ReadinessState{false, false};
        }
    } catch (const std::exception&) {
        return application::ReadinessState{false, false};
    }
    try {
        const auto migration = db_->execSqlSync(R"SQL(
            SELECT COALESCE(
                MAX(version::bigint) FILTER (WHERE version IS NOT NULL), 0)
            FROM flyway_schema_history
            HAVING COALESCE(bool_and(success), FALSE)
        )SQL");
        const auto current = migration.size() == 1
            ? pg::getBigInt(migration[0], 0)
            : 0;
        return application::ReadinessState{
            true, current == expected_migration_version};
    } catch (const std::exception&) {
        return application::ReadinessState{true, false};
    }
}

domain::RepositoryResult<application::OperationalDataSummary>
PostgresOperationsRepository::summary(
    std::chrono::system_clock::time_point now) {
    if (!db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Operations database client is unavailable"));
    }
    try {
        application::OperationalDataSummary result;
        result.outbox_counts = {
            {"pending", 0}, {"processing", 0}, {"published", 0},
            {"failed", 0}, {"deadLetter", 0}};
        const auto counts = db_->execSqlSync(R"SQL(
            SELECT status::text, count(*)
            FROM domain_events_outbox
            GROUP BY status
        )SQL");
        for (const auto& row : counts) {
            auto status = pg::getString(row, 0);
            if (status == "dead_letter") status = "deadLetter";
            if (result.outbox_counts.contains(status)) {
                const auto count = pg::getBigInt(row, 1);
                if (count < 0) {
                    return std::unexpected(domain::RepositoryError::database(
                        "Operational count is invalid"));
                }
                result.outbox_counts[status] = static_cast<std::size_t>(count);
            }
        }

        const auto receipts = db_->execSqlSync(R"SQL(
            SELECT count(*), MAX(handled_at)
            FROM outbox_handler_receipts
        )SQL");
        if (receipts.size() == 1) {
            const auto count = pg::getBigInt(receipts[0], 0);
            if (count >= 0) {
                result.handler_receipt_count = static_cast<std::size_t>(count);
            }
            result.latest_receipt_at = pg::getOptionalTimestamp(receipts[0], 1);
        }

        const auto expired = db_->execSqlSync(
            "SELECT pfh_count_expired_request_idempotency()");
        if (expired.size() == 1) {
            const auto count = pg::getBigInt(expired[0], 0);
            if (count >= 0) {
                result.expired_idempotency_count =
                    static_cast<std::size_t>(count);
            }
        }

        const auto leases = db_->execSqlSync(R"SQL(
            SELECT job_name, lease_until
            FROM scheduled_job_leases
            ORDER BY job_name
        )SQL");
        result.leases.reserve(leases.size());
        for (const auto& row : leases) {
            const auto lease_until = pg::getTimestamp(row, 1);
            result.leases.push_back(application::OperationalLeaseSummary{
                pg::getString(row, 0), lease_until > now, lease_until});
        }
        return result;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "read operational summary", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "read operational summary", error));
    }
}

domain::RepositoryResult<application::DeadLetterPage>
PostgresOperationsRepository::list_dead_letters(
    std::optional<std::string_view> cursor,
    std::size_t limit) {
    if (!db_ || (cursor.has_value() && !is_uuid(*cursor))) {
        return std::unexpected(domain::RepositoryError::validation(
            "Dead-letter query is invalid"));
    }
    try {
        const auto checked = checked_limit(limit);
        const std::optional<std::string> cursor_value = cursor.has_value()
            ? std::optional<std::string>(*cursor)
            : std::nullopt;
        if (cursor_value.has_value()) {
            const auto exists = db_->execSqlSync(
                "SELECT 1 FROM domain_events_outbox "
                "WHERE id = $1::uuid",
                *cursor_value);
            if (exists.empty()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Dead-letter cursor is invalid"));
            }
        }
        const auto rows = db_->execSqlSync(R"SQL(
            SELECT event.id::text, event.event_name,
                   event.aggregate_type, event.aggregate_id,
                   event.retry_count, event.max_retry_count,
                   event.last_failed_handler, event.last_failed_at,
                   event.created_at
            FROM domain_events_outbox AS event
            WHERE event.status = 'dead_letter'::outbox_status
              AND (
                  $1::uuid IS NULL OR
                  (event.created_at, event.id) < (
                      SELECT cursor_event.created_at, cursor_event.id
                      FROM domain_events_outbox AS cursor_event
                      WHERE cursor_event.id = $1::uuid))
            ORDER BY event.created_at DESC, event.id DESC
            LIMIT $2
        )SQL", cursor_value, checked);
        application::DeadLetterPage page;
        page.items.reserve(std::min(rows.size(), limit));
        for (std::size_t index = 0;
             index < rows.size() && index < limit;
             ++index) {
            page.items.push_back(application::DeadLetterSummary{
                pg::getString(rows[index], 0),
                pg::getString(rows[index], 1),
                pg::getOptionalString(rows[index], 2).value_or(""),
                pg::getOptionalString(rows[index], 3).value_or(""),
                rows[index][4].as<int>(),
                rows[index][5].as<int>(),
                pg::getOptionalString(rows[index], 6).value_or(""),
                pg::getOptionalTimestamp(rows[index], 7).value_or(
                    std::chrono::system_clock::time_point{}),
                pg::getTimestamp(rows[index], 8)});
        }
        if (rows.size() > limit && !page.items.empty()) {
            page.next_cursor = page.items.back().id;
        }
        return page;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "list dead letters", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "list dead letters", error));
    }
}

domain::RepositoryResult<application::RetryDeadLetterResult>
PostgresOperationsRepository::retry_dead_letter(
    const application::RetryDeadLetterCommand& command) {
    if (!db_ || !command.operator_user_id.is_valid() ||
        !is_uuid(command.outbox_id) || command.idempotency_key.empty() ||
        command.idempotency_key.size() > 128 || command.trace_id.empty() ||
        command.trace_id.size() > 128) {
        return std::unexpected(domain::RepositoryError::validation(
            "Dead-letter retry request is invalid"));
    }
    try {
        return postgres::execute_transaction<application::RetryDeadLetterResult>(
            db_,
            std::nullopt,
            "retry dead letter",
            [&](const std::shared_ptr<drogon::orm::Transaction>& tx)
                -> domain::RepositoryResult<application::RetryDeadLetterResult> {
                const auto role = tx->execSqlSync(
                    "SELECT role::text FROM users WHERE id = $1 FOR SHARE",
                    command.operator_user_id.value());
                if (role.empty() || pg::getString(role[0], 0) != "operator") {
                    return std::unexpected(domain::RepositoryError::validation(
                        "Operator role is required"));
                }

                const auto event = tx->execSqlSync(R"SQL(
                    SELECT status::text
                    FROM domain_events_outbox
                    WHERE id = $1::uuid
                    FOR UPDATE
                )SQL", command.outbox_id);
                if (event.empty()) {
                    return std::unexpected(domain::RepositoryError::not_found(
                        "Dead letter not found"));
                }

                const auto inserted = tx->execSqlSync(R"SQL(
                    INSERT INTO outbox_retry_commands (
                        operator_user_id, idempotency_key, outbox_id,
                        trace_id, created_at)
                    VALUES ($1, $2, $3::uuid, $4, NOW())
                    ON CONFLICT (operator_user_id, idempotency_key)
                        DO NOTHING
                    RETURNING outbox_id::text
                )SQL",
                    command.operator_user_id.value(),
                    command.idempotency_key,
                    command.outbox_id,
                    command.trace_id);
                if (inserted.empty()) {
                    const auto existing = tx->execSqlSync(R"SQL(
                        SELECT outbox_id::text
                        FROM outbox_retry_commands
                        WHERE operator_user_id = $1 AND idempotency_key = $2
                    )SQL",
                        command.operator_user_id.value(),
                        command.idempotency_key);
                    if (existing.empty() ||
                        pg::getString(existing[0], 0) != command.outbox_id) {
                        return std::unexpected(domain::RepositoryError::conflict(
                            "Idempotency key was used for another dead letter"));
                    }
                    return application::RetryDeadLetterResult{
                        command.outbox_id, true};
                }

                if (pg::getString(event[0], 0) != "dead_letter") {
                    return std::unexpected(domain::RepositoryError::conflict(
                        "Outbox event is not dead-lettered"));
                }
                tx->execSqlSync(R"SQL(
                    UPDATE domain_events_outbox
                    SET status = 'failed'::outbox_status,
                        retry_count = 0,
                        next_retry_at = NOW(),
                        last_error = NULL,
                        last_failed_handler = NULL,
                        last_failed_at = NULL,
                        locked_at = NULL,
                        locked_by = NULL,
                        claim_token = NULL
                    WHERE id = $1::uuid
                )SQL", command.outbox_id);
                tx->execSqlSync(R"SQL(
                    INSERT INTO audit_logs (
                        operator_user_id, actor_type, action,
                        resource_type, resource_id, metadata,
                        trace_id, occurred_at)
                    VALUES (
                        $1, 'operator'::audit_actor_type,
                        'retry'::audit_action, 'Outbox', $2,
                        '{}'::jsonb, $3, NOW())
                )SQL",
                    command.operator_user_id.value(),
                    command.outbox_id,
                    command.trace_id);
                return application::RetryDeadLetterResult{
                    command.outbox_id, false};
            });
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "retry dead letter", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "retry dead letter", error));
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
