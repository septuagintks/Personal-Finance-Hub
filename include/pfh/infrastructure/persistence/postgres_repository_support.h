// Personal Finance Hub - PostgreSQL Repository Support
// Version: 1.0
// C++23

#pragma once

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/repositories/repository_error.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/rls_session.h"

#include <drogon/orm/DbClient.h>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace pfh::infrastructure::postgres {

[[nodiscard]] domain::RepositoryError database_error(
    std::string_view operation,
    const drogon::orm::DrogonDbException& error);

[[nodiscard]] domain::RepositoryError unexpected_error(
    std::string_view operation,
    const std::exception& error);

[[nodiscard]] bool is_lock_conflict(
    const drogon::orm::DrogonDbException& error);

void rollback_noexcept(
    const std::shared_ptr<drogon::orm::Transaction>& transaction,
    std::string_view operation) noexcept;

[[nodiscard]] domain::RepositoryResult<DrogonTransactionContext*>
require_transaction(
    domain::ITransactionContext& context,
    std::optional<domain::UserId> required_tenant = std::nullopt);

/// Execute a tenant-scoped read on one pinned connection. The transaction is
/// deliberately rolled back after the SELECTs: this clears SET LOCAL without
/// relying on pooled-connection affinity and avoids an unnecessary commit.
template <typename T, typename Action>
[[nodiscard]] domain::RepositoryResult<T> execute_tenant_read(
    const drogon::orm::DbClientPtr& db,
    domain::UserId tenant,
    std::string_view operation,
    Action&& action) {
    if (!db) {
        return std::unexpected(domain::RepositoryError::database(
            std::string(operation) + ": database client is unavailable"));
    }
    if (!tenant.is_valid()) {
        return std::unexpected(domain::RepositoryError::validation(
            std::string(operation) + ": invalid tenant scope"));
    }

    std::shared_ptr<drogon::orm::Transaction> transaction;
    try {
        transaction = db->newTransaction();
        if (!transaction) {
            return std::unexpected(domain::RepositoryError::database(
                std::string(operation) + ": transaction is unavailable"));
        }

        RlsSession::setAppUserId(transaction, tenant);
        auto result = std::forward<Action>(action)(transaction);
        rollback_noexcept(transaction, operation);
        transaction.reset();
        return result;
    } catch (const drogon::orm::DrogonDbException& error) {
        rollback_noexcept(transaction, operation);
        return std::unexpected(database_error(operation, error));
    } catch (const std::exception& error) {
        rollback_noexcept(transaction, operation);
        return std::unexpected(unexpected_error(operation, error));
    } catch (...) {
        rollback_noexcept(transaction, operation);
        return std::unexpected(domain::RepositoryError::database(
            std::string(operation) + " failed"));
    }
}

/// Execute writes on one Drogon transaction and wait for the framework's
/// commit callback. Drogon commits when the last Transaction owner is released;
/// issuing a literal COMMIT through execSqlSync bypasses that lifecycle.
template <typename T, typename Action>
[[nodiscard]] domain::RepositoryResult<T> execute_transaction(
    const drogon::orm::DbClientPtr& db,
    std::optional<domain::UserId> tenant,
    std::string_view operation,
    Action&& action) {
    if (!db) {
        return std::unexpected(domain::RepositoryError::database(
            std::string(operation) + ": database client is unavailable"));
    }
    if (tenant.has_value() && !tenant->is_valid()) {
        return std::unexpected(domain::RepositoryError::validation(
            std::string(operation) + ": invalid tenant scope"));
    }

    auto commit_signal = std::make_shared<std::promise<bool>>();
    auto commit_result = commit_signal->get_future();
    std::shared_ptr<drogon::orm::Transaction> transaction;

    try {
        transaction = db->newTransaction(
            [commit_signal](bool committed) noexcept {
                try {
                    commit_signal->set_value(committed);
                } catch (const std::future_error&) {
                    // Drogon promises one callback; ignore a defensive duplicate.
                }
            });
        if (!transaction) {
            return std::unexpected(domain::RepositoryError::database(
                std::string(operation) + ": transaction is unavailable"));
        }

        if (tenant.has_value()) {
            RlsSession::setAppUserId(transaction, *tenant);
        }

        auto result = std::forward<Action>(action)(transaction);
        if (!result.has_value()) {
            rollback_noexcept(transaction, operation);
            transaction.reset();
            return result;
        }

        // Releasing the last owner asks Drogon to commit. The callback is the
        // authoritative completion signal; do not report success before it.
        transaction.reset();
        if (!commit_result.get()) {
            return std::unexpected(domain::RepositoryError::database(
                std::string(operation) + ": transaction commit failed"));
        }
        return result;
    } catch (const drogon::orm::DrogonDbException& error) {
        rollback_noexcept(transaction, operation);
        return std::unexpected(database_error(operation, error));
    } catch (const std::exception& error) {
        rollback_noexcept(transaction, operation);
        return std::unexpected(unexpected_error(operation, error));
    } catch (...) {
        rollback_noexcept(transaction, operation);
        return std::unexpected(domain::RepositoryError::database(
            std::string(operation) + " failed"));
    }
}

}  // namespace pfh::infrastructure::postgres

#endif  // PFH_HAS_POSTGRESQL
