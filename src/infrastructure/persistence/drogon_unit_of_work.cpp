// Personal Finance Hub - Drogon Unit of Work Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/drogon_unit_of_work.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/events/i_domain_event.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/rls_session.h"

#include <spdlog/spdlog.h>
#include <utility>

namespace pfh::infrastructure {

DrogonUnitOfWork::DrogonUnitOfWork(
    drogon::orm::DbClientPtr db,
    domain::UserId user_id)
    : db_(std::move(db)), user_id_(user_id) {}

void DrogonUnitOfWork::register_event(std::shared_ptr<domain::IDomainEvent> event) {
    if (event) {
        pending_events_.push_back(std::move(event));
    }
}

domain::RepositoryVoidResult DrogonUnitOfWork::execute_in_transaction(
    std::function<domain::RepositoryVoidResult(domain::ITransactionContext&)> action) {
    if (!db_) {
        return std::unexpected(domain::RepositoryError::database(
            "DrogonUnitOfWork: null DbClient"));
    }

    auto tx = db_->newTransaction();
    if (!tx) {
        return std::unexpected(domain::RepositoryError::database(
            "DrogonUnitOfWork: failed to begin transaction"));
    }

    // Bind RLS GUC so every statement sees the same tenant. Must be the FIRST
    // statement on the new transaction so the policy fail-closed baseline is
    // never observable by repository SQL.
    try {
        RlsSession::setAppUserId(tx, user_id_);
    } catch (const std::exception& e) {
        // RLS setup failure means the request cannot be safely served.
        tx->rollback();
        return std::unexpected(domain::RepositoryError::database(
            std::string("RLS setup failed: ") + e.what()));
    }

    DrogonTransactionContext ctx(tx);

    // Run user action. Any unexpected return rolls back; outbox events are
    // discarded along with business writes.
    auto result = action(ctx);
    if (!result.has_value()) {
        tx->rollback();
        RlsSession::resetAppUserId(tx);
        // Drop pending events - they belong to the rolled-back business state.
        pending_events_.clear();
        return result;
    }

    // Action succeeded: write outbox events in the same transaction.
    auto outbox_result = write_outbox(ctx);
    if (!outbox_result.has_value()) {
        tx->rollback();
        RlsSession::resetAppUserId(tx);
        pending_events_.clear();
        return outbox_result;
    }

    // Commit. setCommitCallback would publish externally, but Phase 1 leaves
    // outbox publishing to the dedicated OutboxPublisherJob (tasks #34).
    tx->execSqlSync("COMMIT");
    // Drogon's commit semantics: newTransaction returns a transaction whose
    // destructor auto-rolls-back if no explicit commit was issued. Calling
    // COMMIT directly via SQL is acceptable; Drogon's Transaction supports
    // a rollback-on-destruct path which we satisfy by explicitly clearing
    // the GUC before the destructor runs.
    RlsSession::resetAppUserId(tx);

    // Events committed; clear local queue. The outbox row is the durable
    // record; the in-memory copy is no longer needed.
    pending_events_.clear();
    return {};
}

domain::RepositoryVoidResult DrogonUnitOfWork::write_outbox(
    domain::ITransactionContext& tx_iface) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& db_tx = drogon_ctx.transaction();

    if (pending_events_.empty()) {
        return {};
    }

    constexpr const char* kInsertSql = R"SQL(
        INSERT INTO domain_events_outbox (
            id, event_name, aggregate_type, aggregate_id, payload, occurred_at)
        VALUES (gen_random_uuid(), $1, $2, $3, $4::jsonb, $5::timestamptz)
    )SQL";

    try {
        for (const auto& evt : pending_events_) {
            // payload_json is already serialized JSON text (event implementations
            // own their format). Cast inside SQL to keep the call signature
            // simple.
            db_tx.execSqlSync(
                kInsertSql,
                evt->event_name(),                // VARCHAR
                evt->aggregate_type(),            // VARCHAR (nullable)
                evt->aggregate_id(),              // VARCHAR (nullable)
                evt->payload_json(),              // JSONB (text)
                evt->occurred_at());              // TIMESTAMPTZ
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("outbox insert failed: ") + e.base().what()));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL