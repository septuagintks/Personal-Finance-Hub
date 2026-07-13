// Personal Finance Hub - Drogon Transaction Context
// Version: 1.0
// C++23
//
// Wraps a Drogon DbClient transaction handle so repositories can execute SQL
// within a shared transaction boundary without depending on Drogon types.

#pragma once

#include "pfh/application/persistence/i_bootstrap_unit_of_work.h"
#include "pfh/domain/user.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>
#include <memory>
#include <optional>

namespace pfh::infrastructure {

/// @brief Opaque handle wrapping a Drogon database transaction.
///
/// Lifetime: created by DrogonUnitOfWork::execute_in_transaction, destroyed
/// when the transaction closure returns. Repositories receive a reference and
/// must not hold it beyond the closure boundary.
class DrogonTransactionContext final
    : public application::ITenantBootstrapTransaction {
public:
    explicit DrogonTransactionContext(
        const std::shared_ptr<drogon::orm::Transaction>& tx,
        std::optional<domain::UserId> tenant_user_id = std::nullopt)
        : tx_(tx), tenant_user_id_(tenant_user_id) {}

    /// @brief Access the underlying Drogon transaction for SQL execution.
    /// Non-const: execCommand/execSqlAsync mutate the transaction state.
    [[nodiscard]] drogon::orm::Transaction& transaction() const { return *tx_; }
    [[nodiscard]] bool is_valid() const noexcept { return tx_ != nullptr; }
    [[nodiscard]] std::optional<domain::UserId> tenant_user_id() const noexcept override {
        return tenant_user_id_;
    }

    [[nodiscard]] domain::RepositoryVoidResult bind_tenant_once(
        domain::UserId user_id) override;

private:
    std::shared_ptr<drogon::orm::Transaction> tx_;
    std::optional<domain::UserId> tenant_user_id_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
