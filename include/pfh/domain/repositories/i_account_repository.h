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

struct AccountBalanceAt {
    Account account;
    Money balance;
};

struct BalanceCacheRebuildResult {
    BalanceSnapshot snapshot;
    std::int64_t source_version = 0;
    std::int64_t cache_version = 1;
};

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

    /// @brief List owned accounts by archive state. nullopt returns all rows.
    [[nodiscard]] virtual RepositoryResult<std::vector<Account>> find_by_user(
        UserId user_id,
        std::optional<bool> archived) = 0;

    /// @brief Check whether any active or deleted transaction references the
    /// account inside the current write transaction.
    [[nodiscard]] virtual RepositoryResult<bool> has_transactions(
        ITransactionContext& tx,
        AccountId id) = 0;

    /// @brief Return balance snapshot. Implementation owns cache hit/miss/rebuild.
    [[nodiscard]] virtual RepositoryResult<BalanceSnapshot> balance_of(AccountId id) = 0;

    [[nodiscard]] virtual RepositoryResult<std::vector<BalanceCacheRebuildResult>>
    rebuild_balance_cache(
        ITransactionContext& tx,
        UserId user_id,
        std::optional<AccountId> account_id,
        std::chrono::system_clock::time_point rebuilt_at) = 0;

    /// @brief Return tenant-owned account balances at an inclusive historical
    /// valuation instant. Account created_at is record metadata, not a business
    /// opening date, so backdated entries remain visible. Accounts archived
    /// at/before the instant are excluded; active transactions with
    /// occurred_at <= as_of are summed using their persisted signed amount.
    [[nodiscard]] virtual RepositoryResult<std::vector<AccountBalanceAt>>
    balances_at(
        UserId user_id,
        std::chrono::system_clock::time_point as_of) = 0;

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
