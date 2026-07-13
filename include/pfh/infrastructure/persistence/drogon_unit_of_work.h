// Personal Finance Hub - Drogon Unit of Work
// Version: 1.0
// C++23
//
// Production IUnitOfWork implementation: shares one Drogon transaction across
// all repository writes in the action closure, writes outbox rows in the same
// transaction, sets RLS GUC at transaction start and clears it at end.

#pragma once

#include "pfh/application/persistence/i_unit_of_work.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>
#include <memory>
#include <vector>

namespace pfh::infrastructure {

/// @brief Production Unit of Work backed by Drogon's PostgreSQL transaction.
///
/// Lifecycle:
///   1. execute_in_transaction opens a DB transaction via newTransaction().
///   2. Sets app.current_user_id GUC (RLS binding).
///   3. Wraps the tx in DrogonTransactionContext and calls action closure.
///   4. On success: writes pending outbox events, commits transaction, clears GUC.
///   5. On failure: rollback (discards business + outbox), clears GUC, propagates error.
///
/// The GUC is cleared before returning the connection to the pool so the next
/// request cannot observe stale tenant context.
class DrogonUnitOfWork final : public application::IUnitOfWork {
public:
    /// @param db Drogon database client (pooled connection manager).
    /// @param user_id Authenticated user from JWT; bound to app.current_user_id.
    explicit DrogonUnitOfWork(
        drogon::orm::DbClientPtr db,
        domain::UserId user_id);

    void register_event(std::shared_ptr<domain::IDomainEvent> event) override;

    [[nodiscard]] domain::RepositoryVoidResult execute_in_transaction(
        std::function<domain::RepositoryVoidResult(domain::ITransactionContext& tx)>
            action) override;

private:
    /// @brief Write all pending events to domain_events_outbox in the active tx.
    [[nodiscard]] domain::RepositoryVoidResult write_outbox(
        domain::ITransactionContext& tx);

    drogon::orm::DbClientPtr db_;
    domain::UserId user_id_;
    std::vector<std::shared_ptr<domain::IDomainEvent>> pending_events_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
