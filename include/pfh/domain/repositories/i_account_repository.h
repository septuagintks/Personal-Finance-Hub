// Personal Finance Hub - Account Repository Interface
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/account.h"
#include "pfh/domain/balance_calculation_service.h"
#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include <vector>

namespace pfh::domain {

class IAccountRepository {
public:
    virtual ~IAccountRepository() = default;

    [[nodiscard]] virtual RepositoryResult<Account> find_by_id(AccountId id) = 0;

    /// @brief Find account and enforce ownership (user isolation).
    [[nodiscard]] virtual RepositoryResult<Account> find_by_id_for_user(
        AccountId id,
        UserId user_id) = 0;

    /// @brief Load an owned account inside the active transaction and take a row
    /// lock on it (PostgreSQL `SELECT ... FOR UPDATE`).
    ///
    /// This is the write-path read boundary: use cases that mutate balances
    /// (posting, transfer, import) must acquire the account lock here so the
    /// ownership/archive checks and the subsequent writes see a consistent,
    /// serialized view of the row. To prevent deadlocks, callers locking more
    /// than one account MUST lock in ascending account id order (design §4.1).
    /// Requires an active transaction context.
    [[nodiscard]] virtual RepositoryResult<Account> find_by_id_for_update(
        ITransactionContext& tx,
        AccountId id,
        UserId user_id) = 0;

    [[nodiscard]] virtual RepositoryResult<std::vector<Account>> find_active_by_user(
        UserId user_id) = 0;

    /// @brief Return balance snapshot. Implementation owns cache hit/miss/rebuild.
    [[nodiscard]] virtual RepositoryResult<BalanceSnapshot> balance_of(AccountId id) = 0;

    /// @brief Insert or update. Optimistic lock: update requires matching version.
    [[nodiscard]] virtual RepositoryResult<AccountId> save(
        ITransactionContext& tx,
        const Account& account) = 0;

    [[nodiscard]] virtual RepositoryVoidResult delete_balance_cache(
        ITransactionContext& tx,
        AccountId id) = 0;

    [[nodiscard]] virtual RepositoryVoidResult physical_delete(
        ITransactionContext& tx,
        AccountId id) = 0;
};

} // namespace pfh::domain
