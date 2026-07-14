// Personal Finance Hub - PostgreSQL Idempotent Supplemental Audit Store

#include "pfh/infrastructure/persistence/postgres_supplemental_audit_store.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/audit_log_repository_impl.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/postgres_repository_support.h"

#include <string>

namespace pfh::infrastructure {

domain::RepositoryResult<bool> PostgresSupplementalAuditStore::append_once(
    std::string_view outbox_id,
    std::string_view handler_name,
    const domain::AuditLogEntry& entry) {
    if (!db_ || outbox_id.empty() || handler_name.empty() ||
        handler_name.size() > 128 ||
        entry.actor_type != domain::AuditActorType::System ||
        entry.operator_user_id.has_value()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Invalid supplemental audit record"));
    }

    AuditLogRepositoryImpl audit_logs;
    return postgres::execute_transaction<bool>(
        db_,
        std::nullopt,
        "append supplemental audit",
        [&](const std::shared_ptr<drogon::orm::Transaction>& transaction)
            -> domain::RepositoryResult<bool> {
            constexpr const char* kReceiptSql = R"SQL(
                INSERT INTO outbox_handler_receipts (
                    outbox_id, handler_name)
                VALUES ($1::uuid, $2)
                ON CONFLICT (outbox_id, handler_name) DO NOTHING
                RETURNING outbox_id
            )SQL";
            const auto receipt = transaction->execSqlSync(
                kReceiptSql,
                std::string(outbox_id),
                std::string(handler_name));
            if (receipt.empty()) {
                return false;
            }

            DrogonTransactionContext context(transaction, std::nullopt);
            auto appended = audit_logs.append(context, entry);
            if (!appended) {
                return std::unexpected(appended.error());
            }
            return true;
        });
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
