// Personal Finance Hub - PostgreSQL Request Idempotency Repository

#include "pfh/infrastructure/persistence/idempotency_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <nlohmann/json.hpp>

namespace pfh::infrastructure {

domain::RepositoryResult<application::IdempotencyBeginResult>
IdempotencyRepositoryImpl::begin(
    domain::ITransactionContext& tx_iface,
    domain::UserId user_id,
    std::string_view operation,
    std::string_view key,
    std::string_view request_fingerprint,
    std::chrono::system_clock::time_point created_at,
    std::chrono::system_clock::time_point expires_at) {
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context || user_id != tenant_user_id_) {
        return std::unexpected(context
            ? domain::RepositoryError::not_found("Idempotency record not found")
            : context.error());
    }
    constexpr const char* kInsertSql = R"SQL(
        INSERT INTO request_idempotency (
            user_id, operation, idempotency_key, request_fingerprint,
            status, created_at, expires_at)
        VALUES ($1, $2, $3, $4, 'in_progress', $5::timestamptz, $6::timestamptz)
        ON CONFLICT (user_id, operation, idempotency_key) DO NOTHING
    )SQL";
    constexpr const char* kDeleteExpiredSql = R"SQL(
        DELETE FROM request_idempotency
        WHERE user_id = $1 AND operation = $2 AND idempotency_key = $3
          AND expires_at <= $4::timestamptz
    )SQL";
    constexpr const char* kSelectSql = R"SQL(
        SELECT request_fingerprint, status::text, response_values::text
        FROM request_idempotency
        WHERE user_id = $1 AND operation = $2 AND idempotency_key = $3
        FOR UPDATE
    )SQL";
    try {
        auto& tx = (*context)->transaction();
        tx.execSqlSync(
            kDeleteExpiredSql,
            user_id.value(),
            std::string(operation),
            std::string(key),
            pg::toDbTimestamp(created_at));
        const auto inserted = tx.execSqlSync(
            kInsertSql,
            user_id.value(),
            std::string(operation),
            std::string(key),
            std::string(request_fingerprint),
            pg::toDbTimestamp(created_at),
            pg::toDbTimestamp(expires_at));
        const auto rows = tx.execSqlSync(
            kSelectSql,
            user_id.value(),
            std::string(operation),
            std::string(key));
        if (rows.size() != 1U) {
            return std::unexpected(domain::RepositoryError::database(
                "Idempotency row could not be locked"));
        }
        if (pg::getString(rows[0], 0) != request_fingerprint) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Idempotency key was already used with another request"));
        }
        if (inserted.affectedRows() == 1U) {
            return application::IdempotencyBeginResult{};
        }
        if (pg::getString(rows[0], 1) != "completed") {
            return std::unexpected(domain::RepositoryError::conflict(
                "Idempotent request is still in progress"));
        }
        const auto payload = nlohmann::json::parse(pg::getString(rows[0], 2));
        if (!payload.is_object()) {
            return std::unexpected(domain::RepositoryError::database(
                "Idempotency response is invalid"));
        }
        std::map<std::string, std::string> values;
        for (const auto& [name, value] : payload.items()) {
            if (!value.is_string()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Idempotency response is invalid"));
            }
            values.emplace(name, value.get<std::string>());
        }
        return application::IdempotencyBeginResult{true, std::move(values)};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error("idempotency begin", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("idempotency begin", error));
    }
}

domain::RepositoryVoidResult IdempotencyRepositoryImpl::complete(
    domain::ITransactionContext& tx_iface,
    domain::UserId user_id,
    std::string_view operation,
    std::string_view key,
    const std::map<std::string, std::string>& response_values) {
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context || user_id != tenant_user_id_) {
        return std::unexpected(context
            ? domain::RepositoryError::not_found("Idempotency record not found")
            : context.error());
    }
    constexpr const char* kSql = R"SQL(
        UPDATE request_idempotency
        SET status = 'completed', response_values = $4::jsonb
        WHERE user_id = $1 AND operation = $2 AND idempotency_key = $3
          AND status = 'in_progress'
    )SQL";
    try {
        const nlohmann::json payload(response_values);
        const auto result = (*context)->transaction().execSqlSync(
            kSql,
            user_id.value(),
            std::string(operation),
            std::string(key),
            payload.dump());
        if (result.affectedRows() != 1U) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Idempotency request is not pending"));
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error("idempotency complete", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("idempotency complete", error));
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
