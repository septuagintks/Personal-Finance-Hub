// Personal Finance Hub - Drogon Transaction Context
// Version: 1.0
// C++23
//
// Wraps a Drogon DbClient transaction handle so repositories can execute SQL
// within a shared transaction boundary without depending on Drogon types.

#pragma once

#include "pfh/domain/repositories/i_transaction_context.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>
#include <memory>

namespace pfh::infrastructure {

/// @brief Opaque handle wrapping a Drogon database transaction.
///
/// Lifetime: created by DrogonUnitOfWork::execute_in_transaction, destroyed
/// when the transaction closure returns. Repositories receive a reference and
/// must not hold it beyond the closure boundary.
class DrogonTransactionContext final : public domain::ITransactionContext {
public:
    explicit DrogonTransactionContext(
        const std::shared_ptr<drogon::orm::Transaction>& tx)
        : tx_(tx) {}

    /// @brief Access the underlying Drogon transaction for SQL execution.
    /// Non-const: execCommand/execSqlAsync mutate the transaction state.
    [[nodiscard]] drogon::orm::Transaction& transaction() const { return *tx_; }

private:
    std::shared_ptr<drogon::orm::Transaction> tx_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
