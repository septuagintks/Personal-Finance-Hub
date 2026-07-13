// Personal Finance Hub - Drogon Transaction Context Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/drogon_transaction_context.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/rls_session.h"

namespace pfh::infrastructure {

domain::RepositoryVoidResult DrogonTransactionContext::bind_tenant_once(
    domain::UserId user_id) {
    if (!is_valid()) {
        return std::unexpected(domain::RepositoryError::database(
            "Cannot bind tenant on an invalid transaction"));
    }
    if (!user_id.is_valid()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Cannot bind an invalid tenant user id"));
    }
    if (tenant_user_id_.has_value()) {
        return std::unexpected(domain::RepositoryError::conflict(
            "Transaction tenant is already bound"));
    }
    try {
        RlsSession::setAppUserId(tx_, user_id);
        tenant_user_id_ = user_id;
        return {};
    } catch (const drogon::orm::DrogonDbException&) {
        return std::unexpected(domain::RepositoryError::database(
            "Failed to bind registration tenant"));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Failed to bind registration tenant"));
    }
}

} // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
