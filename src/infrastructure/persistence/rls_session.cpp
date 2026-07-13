// Personal Finance Hub - RLS Session Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/rls_session.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/repositories/repository_error.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

namespace pfh::infrastructure {

namespace {

// Prepared statement that becomes a no-op on rollback (only side effect is
// the session-local GUC). current_setting('app.current_user_id', TRUE) below
// reads the value inside pfh_current_user_id().

constexpr const char* kSetGucSql =
    "SELECT set_config('app.current_user_id', $1, true)";

constexpr const char* kResetGucSql =
    "SELECT set_config('app.current_user_id', '', false)";

[[noreturn]] void throwUnauthenticated() {
    throw std::invalid_argument(
        "RlsSession: refuse to bind an empty user_id; "
        "authentication must precede database access");
}

}  // namespace

void RlsSession::setAppUserId(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    domain::UserId user_id) {
    if (!tx) {
        throw std::invalid_argument("RlsSession: null transaction");
    }
    if (user_id.value <= 0) {
        throwUnauthenticated();
    }
    const auto uid_str = std::to_string(user_id.value);
    try {
        tx->execSqlSync(kSetGucSql, uid_str);
    } catch (const drogon::orm::DrogonDbException& e) {
        spdlog::error("RLS set failed: {}", e.base().what());
        throw;
    }
}

void RlsSession::resetAppUserId(
    const std::shared_ptr<drogon::orm::Transaction>& tx) {
    if (!tx) return;
    try {
        tx->execSqlSync(kResetGucSql);
    } catch (const drogon::orm::DrogonDbException& e) {
        // Connection cleanup MUST NOT throw; just log. The connection is
        // about to be returned to the pool and the GUC has its own scope.
        spdlog::warn("RLS reset failed (ignored): {}", e.base().what());
    }
}

void RlsSession::setAppUserId(
    const drogon::orm::DbClientPtr& db,
    domain::UserId user_id) {
    if (!db) {
        throw std::invalid_argument("RlsSession: null db client");
    }
    if (user_id.value <= 0) {
        throwUnauthenticated();
    }
    const auto uid_str = std::to_string(user_id.value);
    try {
        db->execSqlSync(kSetGucSql, uid_str);
    } catch (const drogon::orm::DrogonDbException& e) {
        spdlog::error("RLS set failed: {}", e.base().what());
        throw;
    }
}

void RlsSession::resetAppUserId(const drogon::orm::DbClientPtr& db) {
    if (!db) return;
    try {
        db->execSqlSync(kResetGucSql);
    } catch (const drogon::orm::DrogonDbException& e) {
        spdlog::warn("RLS reset failed (ignored): {}", e.base().what());
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL