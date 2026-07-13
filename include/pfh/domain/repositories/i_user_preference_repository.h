// Personal Finance Hub - UserPreference Repository Interface
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/user_preference.h"

namespace pfh::domain {

class IUserPreferenceRepository {
public:
    virtual ~IUserPreferenceRepository() = default;

    /// @brief Load preference for user. Falls back to users.base_currency_code
    /// defaults when no preference row exists.
    [[nodiscard]] virtual RepositoryResult<UserPreference> find_by_user(UserId user_id) = 0;

    /// Transaction-aware variant used when the caller must observe preferences
    /// or users.base_currency_code written earlier in the same Unit of Work.
    [[nodiscard]] virtual RepositoryResult<UserPreference> find_by_user(
        ITransactionContext& tx,
        UserId user_id) = 0;

    [[nodiscard]] virtual RepositoryVoidResult save(
        ITransactionContext& tx,
        const UserPreference& preference) = 0;
};

} // namespace pfh::domain
