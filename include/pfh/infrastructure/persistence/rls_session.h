// Personal Finance Hub - RLS Session Helper
// Version: 1.0
// C++23
//
// Per-request / per-transaction lifecycle for the PostgreSQL Row-Level Security
// GUC `app.current_user_id`. Without this set, the RLS policies fail-closed:
// current_setting returns NULL, USING/WITH CHECK match no rows, the request
// returns empty.
//
// The GUC is transaction-local. Every tenant operation must pin one Drogon
// Transaction, set the value before its first tenant query, then commit or
// rollback. A SET on DbClient is unsafe because consecutive pooled statements
// are not guaranteed to use the same physical connection.
//
// Authorization is the responsibility of the caller: this helper is purely a
// database-side binding for the JWT-derived user_id.

#pragma once

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/user.h"
#include <drogon/orm/DbClient.h>
#include <memory>

namespace pfh::infrastructure {

/// @brief Apply `app.current_user_id` to an active transaction.
class RlsSession {
public:
    /// @brief Set GUC on an active transaction (called at tx begin).
    /// Uses parameter binding to avoid SQL injection. Empty user_id is treated
    /// as an authorization precondition failure: callers must never call this
    /// with an unauthenticated context.
    static void setAppUserId(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        domain::UserId user_id);

};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
