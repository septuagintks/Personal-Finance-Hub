// Personal Finance Hub - Transaction Repository Interface
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/transaction.h"
#include "pfh/domain/transfer_aggregate.h"
#include <chrono>
#include <optional>
#include <vector>

namespace pfh::domain {

class ITransactionRepository {
public:
    virtual ~ITransactionRepository() = default;

    [[nodiscard]] virtual RepositoryResult<Transaction> find_by_id(TransactionId id) = 0;

    [[nodiscard]] virtual RepositoryResult<TransactionId> save_single(
        ITransactionContext& tx,
        const Transaction& transaction) = 0;

    /// @brief Persist a TransferAggregate atomically:
    /// transfer_groups row + outgoing + incoming (+ adjustments).
    [[nodiscard]] virtual RepositoryResult<TransferGroupId> save_transfer(
        ITransactionContext& tx,
        const TransferAggregate& transfer) = 0;

    [[nodiscard]] virtual RepositoryResult<std::vector<Transaction>> find_by_account(
        AccountId account_id,
        std::optional<std::chrono::system_clock::time_point> from = std::nullopt,
        std::optional<std::chrono::system_clock::time_point> to = std::nullopt,
        bool include_deleted = false) = 0;

    [[nodiscard]] virtual RepositoryResult<std::vector<Transaction>> find_by_user(
        UserId user_id,
        bool include_deleted = false) = 0;

    [[nodiscard]] virtual RepositoryVoidResult soft_delete(
        ITransactionContext& tx,
        TransactionId id,
        UserId user_id,
        std::chrono::system_clock::time_point deleted_at) = 0;

    [[nodiscard]] virtual RepositoryVoidResult physical_delete_by_account(
        ITransactionContext& tx,
        AccountId account_id) = 0;
};

} // namespace pfh::domain
