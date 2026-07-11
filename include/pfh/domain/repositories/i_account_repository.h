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

    [[nodiscard]] virtual RepositoryResult<std::vector<Account>> find_active_by_user(
        UserId user_id) = 0;

    [[nodiscard]] virtual RepositoryResult<std::vector<Currency>> find_active_currencies() = 0;

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
