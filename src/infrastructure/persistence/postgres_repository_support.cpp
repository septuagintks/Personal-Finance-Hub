// Personal Finance Hub - PostgreSQL Repository Support Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/postgres_repository_support.h"

#ifdef PFH_HAS_POSTGRESQL

#include <spdlog/spdlog.h>

namespace pfh::infrastructure::postgres {

domain::RepositoryError database_error(
    std::string_view operation,
    const drogon::orm::DrogonDbException& error) {
    spdlog::error("PostgreSQL {} failed: {}", operation, error.base().what());
    return domain::RepositoryError::database(
        std::string(operation) + " failed");
}

domain::RepositoryError unexpected_error(
    std::string_view operation,
    const std::exception& error) {
    spdlog::error("PostgreSQL {} raised an unexpected exception: {}",
                  operation,
                  error.what());
    return domain::RepositoryError::database(
        std::string(operation) + " failed");
}

bool is_lock_conflict(const drogon::orm::DrogonDbException& error) {
    const std::string_view detail(error.base().what());
    return detail.find("55P03") != std::string_view::npos ||
           detail.find("could not obtain lock on row") !=
               std::string_view::npos;
}

void rollback_noexcept(
    const std::shared_ptr<drogon::orm::Transaction>& transaction,
    std::string_view operation) noexcept {
    if (!transaction) {
        return;
    }
    try {
        transaction->rollback();
    } catch (const std::exception& error) {
        spdlog::warn("PostgreSQL {} rollback failed: {}", operation, error.what());
    } catch (...) {
        spdlog::warn("PostgreSQL {} rollback failed with an unknown exception",
                     operation);
    }
}

domain::RepositoryResult<DrogonTransactionContext*> require_transaction(
    domain::ITransactionContext& context,
    std::optional<domain::UserId> required_tenant) {
    auto* drogon_context = dynamic_cast<DrogonTransactionContext*>(&context);
    if (drogon_context == nullptr || !drogon_context->is_valid()) {
        return std::unexpected(domain::RepositoryError::database(
            "Invalid PostgreSQL transaction context"));
    }

    if (required_tenant.has_value() &&
        drogon_context->tenant_user_id() != required_tenant) {
        return std::unexpected(domain::RepositoryError::validation(
            "PostgreSQL transaction tenant scope mismatch"));
    }
    return drogon_context;
}

}  // namespace pfh::infrastructure::postgres

#endif  // PFH_HAS_POSTGRESQL
