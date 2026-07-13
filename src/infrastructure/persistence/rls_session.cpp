// Personal Finance Hub - RLS Session Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/rls_session.h"

#ifdef PFH_HAS_POSTGRESQL

#include <stdexcept>
#include <string>

namespace pfh::infrastructure {

namespace {

// Prepared statement that becomes a no-op on rollback (only side effect is
// the session-local GUC). current_setting('app.current_user_id', TRUE) below
// reads the value inside pfh_current_user_id().

constexpr const char* kSetGucSql =
    "SELECT set_config('app.current_user_id', $1, true)";

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
    if (!user_id.is_valid()) {
        throwUnauthenticated();
    }
    tx->execSqlSync(kSetGucSql, std::to_string(user_id.value()));
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
