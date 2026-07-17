// Personal Finance Hub - PostgreSQL Audit Log Repository

#include "pfh/infrastructure/persistence/audit_log_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace pfh::infrastructure {

namespace {

[[nodiscard]] std::string action_text(domain::AuditAction action) {
    using domain::AuditAction;
    switch (action) {
    case AuditAction::Create: return "create";
    case AuditAction::Update: return "update";
    case AuditAction::Archive: return "archive";
    case AuditAction::Delete: return "delete";
    case AuditAction::DangerousDelete: return "dangerous_delete";
    case AuditAction::SyncImport: return "sync_import";
    case AuditAction::Refresh: return "refresh";
    case AuditAction::Register: return "register";
    case AuditAction::Login: return "login";
    case AuditAction::Logout: return "logout";
    case AuditAction::TokenRefresh: return "token_refresh";
    case AuditAction::SecurityEvent: return "security_event";
    case AuditAction::Retry: return "retry";
    }
    return "security_event";
}

[[nodiscard]] domain::AuditAction parse_action(std::string_view value) {
    using domain::AuditAction;
    if (value == "create") return AuditAction::Create;
    if (value == "update") return AuditAction::Update;
    if (value == "archive") return AuditAction::Archive;
    if (value == "delete") return AuditAction::Delete;
    if (value == "dangerous_delete") return AuditAction::DangerousDelete;
    if (value == "sync_import") return AuditAction::SyncImport;
    if (value == "refresh") return AuditAction::Refresh;
    if (value == "register") return AuditAction::Register;
    if (value == "login") return AuditAction::Login;
    if (value == "logout") return AuditAction::Logout;
    if (value == "token_refresh") return AuditAction::TokenRefresh;
    if (value == "security_event") return AuditAction::SecurityEvent;
    if (value == "retry") return AuditAction::Retry;
    throw std::invalid_argument("unknown audit action");
}

[[nodiscard]] const char* actor_text(domain::AuditActorType actor) noexcept {
    if (actor == domain::AuditActorType::Operator) return "operator";
    if (actor == domain::AuditActorType::System) return "system";
    return "user";
}

} // namespace

domain::RepositoryVoidResult AuditLogRepositoryImpl::append(
    domain::ITransactionContext& tx_iface,
    const domain::AuditLogEntry& entry) {
    if ((entry.actor_type == domain::AuditActorType::User ||
         entry.actor_type == domain::AuditActorType::Operator) &&
        (!entry.operator_user_id.has_value() ||
         !entry.operator_user_id->is_valid())) {
        return std::unexpected(domain::RepositoryError::validation(
            "User audit actor is required"));
    }
    if (entry.actor_type == domain::AuditActorType::System &&
        entry.operator_user_id.has_value()) {
        return std::unexpected(domain::RepositoryError::validation(
            "System audit must not carry a user actor"));
    }
    auto context = postgres::require_transaction(
        tx_iface, entry.operator_user_id);
    if (!context) {
        return std::unexpected(context.error());
    }
    if (entry.actor_type == domain::AuditActorType::System &&
        (*context)->tenant_user_id().has_value()) {
        return std::unexpected(domain::RepositoryError::validation(
            "System audit requires an unscoped transaction"));
    }
    if (entry.resource_type.empty() || entry.resource_id.empty()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Audit resource is required"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            INSERT INTO audit_logs (
                operator_user_id, actor_type, action, resource_type, resource_id,
                before_value, after_value, metadata, trace_id, occurred_at)
            VALUES (
                $1, $2::audit_actor_type, $3::audit_action, $4, $5,
                NULLIF($6, '')::jsonb,
                NULLIF($7, '')::jsonb,
                COALESCE(NULLIF($8, '')::jsonb, '{}'::jsonb),
                NULLIF($9, ''), $10)
        )SQL";
        const std::optional<std::int64_t> operator_id =
            entry.operator_user_id.has_value()
                ? std::optional<std::int64_t>(entry.operator_user_id->value())
                : std::nullopt;
        (*context)->transaction().execSqlSync(
            kSql,
            operator_id,
            actor_text(entry.actor_type),
            action_text(entry.action),
            entry.resource_type,
            entry.resource_id,
            entry.before_value_json,
            entry.after_value_json,
            entry.metadata_json,
            entry.trace_id,
            pg::toDbTimestamp(entry.occurred_at));
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "append audit log", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "append audit log", error));
    }
}

domain::RepositoryResult<domain::UserAuditLogPage>
AuditLogRepositoryImpl::find_user_entries(
    domain::ITransactionContext& tx_iface,
    const domain::UserAuditLogQuery& query) {
    if (!query.user_id.is_valid() || query.limit == 0 || query.limit > 100 ||
        (query.resource_type.has_value() &&
         (query.resource_type->empty() || query.resource_type->size() > 64)) ||
        (query.before_id.has_value() && *query.before_id <= 0) ||
        (query.from.has_value() && query.to.has_value() &&
         *query.from > *query.to)) {
        return std::unexpected(domain::RepositoryError::validation(
            "Audit query is invalid"));
    }
    auto context = postgres::require_transaction(tx_iface, query.user_id);
    if (!context) return std::unexpected(context.error());

    try {
        constexpr const char* kSql = R"SQL(
            SELECT id, action::text, resource_type, resource_id,
                   trace_id, occurred_at
            FROM audit_logs
            WHERE operator_user_id = $1
              AND actor_type = 'user'::audit_actor_type
              AND ($2::text IS NULL OR action = $2::audit_action)
              AND ($3::text IS NULL OR resource_type = $3)
              AND ($4::timestamptz IS NULL OR occurred_at >= $4)
              AND ($5::timestamptz IS NULL OR occurred_at < $5)
              AND ($6::bigint IS NULL OR id < $6)
            ORDER BY id DESC
            LIMIT $7
        )SQL";
        const std::optional<std::string> action = query.action.has_value()
            ? std::optional<std::string>(action_text(*query.action))
            : std::nullopt;
        const auto limit = static_cast<std::int64_t>(query.limit + 1);
        const auto rows = (*context)->transaction().execSqlSync(
            kSql,
            query.user_id.value(),
            action,
            query.resource_type,
            pg::toDbTimestamp(query.from),
            pg::toDbTimestamp(query.to),
            query.before_id,
            limit);

        domain::UserAuditLogPage page;
        page.entries.reserve(std::min(rows.size(), query.limit));
        for (std::size_t index = 0;
             index < rows.size() && index < query.limit;
             ++index) {
            domain::AuditLogEntry entry;
            entry.id = pg::getBigInt(rows[index], 0);
            entry.operator_user_id = query.user_id;
            entry.actor_type = domain::AuditActorType::User;
            entry.action = parse_action(pg::getString(rows[index], 1));
            entry.resource_type = pg::getString(rows[index], 2);
            entry.resource_id = pg::getString(rows[index], 3);
            entry.trace_id = pg::getOptionalString(rows[index], 4).value_or("");
            entry.occurred_at = pg::getTimestamp(rows[index], 5);
            page.entries.push_back(std::move(entry));
        }
        if (rows.size() > query.limit && !page.entries.empty()) {
            page.next_before_id = page.entries.back().id;
        }
        return page;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "query user audit logs", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "query user audit logs", error));
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
