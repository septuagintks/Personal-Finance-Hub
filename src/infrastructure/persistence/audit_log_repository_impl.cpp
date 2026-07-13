// Personal Finance Hub - PostgreSQL Audit Log Repository

#include "pfh/infrastructure/persistence/audit_log_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

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
    }
    return "security_event";
}

} // namespace

domain::RepositoryVoidResult AuditLogRepositoryImpl::append(
    domain::ITransactionContext& tx_iface,
    const domain::AuditLogEntry& entry) {
    auto context = postgres::require_transaction(
        tx_iface, entry.operator_user_id);
    if (!context) {
        return std::unexpected(context.error());
    }
    if (entry.resource_type.empty() || entry.resource_id.empty()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Audit resource is required"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            INSERT INTO audit_logs (
                operator_user_id, action, resource_type, resource_id,
                before_value, after_value, metadata, occurred_at)
            VALUES (
                $1, $2::audit_action, $3, $4,
                NULLIF($5, '')::jsonb,
                NULLIF($6, '')::jsonb,
                COALESCE(NULLIF($7, '')::jsonb, '{}'::jsonb),
                $8)
        )SQL";
        (*context)->transaction().execSqlSync(
            kSql,
            entry.operator_user_id.value(),
            action_text(entry.action),
            entry.resource_type,
            entry.resource_id,
            entry.before_value_json,
            entry.after_value_json,
            entry.metadata_json,
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

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
