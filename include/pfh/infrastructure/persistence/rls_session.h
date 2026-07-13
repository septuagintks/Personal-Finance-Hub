// Personal Finance Hub - RLS Session Helper
// Version: 1.0
// C++23
//
// Per-request / per-transaction lifecycle for the PostgreSQL Row-Level Security
// GUC `app.current_user_id`. Without this set, the RLS policies fail-closed:
// current_setting returns NULL, USING/WITH CHECK match no rows, the request
// returns empty.
//
// Two usage models are supported:
//   1. **Pooled connection per request** (typical for HTTP request handlers):
//      acquire → setAppUserId → work → reset → release. The reset is required
//      to avoid leaking the previous tenant id into the next request that
//      happens to reuse the connection.
//   2. **Pooled connection per transaction** (used inside DrogonUnitOfWork):
//      the SET is issued at the start of the transaction and RESET on commit
//      or rollback. This guarantees the GUC is cleared before the connection
//      is returned to the pool.
//
// Authorization is the responsibility of the caller: this helper is purely a
// database-side binding for the JWT-derived user_id.

#pragma once

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/user.h"
#include <drogon/orm/DbClient.h>
#include <memory>

namespace pfh::infrastructure {

/// @brief Apply the `app.current_user_id` GUC on a connection or transaction.
///
/// Drogon's DbClient exposes two executors:
///   - `execSqlSync(sql, ...)` runs against a pooled connection.
///   - `Transaction::execSqlSync(sql, ...)` runs inside the active transaction.
///
/// Both must set the GUC because RLS policies depend on it. `setAppUserId`
/// dispatches based on which overload it was given.
class RlsSession {
public:
    /// @brief Set GUC on an active transaction (called at tx begin).
    /// Uses parameter binding to avoid SQL injection. Empty user_id is treated
    /// as an authorization precondition failure: callers must never call this
    /// with an unauthenticated context.
    static void setAppUserId(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        domain::UserId user_id);

    /// @brief Clear the GUC (called at tx end or connection release).
    static void resetAppUserId(
        const std::shared_ptr<drogon::orm::Transaction>& tx);

    /// @brief Set GUC on a freshly acquired pooled connection (request-scoped).
    static void setAppUserId(
        const drogon::orm::DbClientPtr& db,
        domain::UserId user_id);

    /// @brief Clear GUC on a pooled connection before release.
    static void resetAppUserId(const drogon::orm::DbClientPtr& db);
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL