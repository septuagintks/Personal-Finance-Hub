// Personal Finance Hub - User Repository Interface
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/user.h"
#include <string>

namespace pfh::domain {

class IUserRepository {
public:
    virtual ~IUserRepository() = default;

    [[nodiscard]] virtual RepositoryResult<User> find_by_id(UserId id) = 0;
    [[nodiscard]] virtual RepositoryResult<User> find_by_username(const std::string& username) = 0;

    /// Transaction-aware reads for workflows that must observe an earlier
    /// create/update before commit (for example registration bootstrap).
    [[nodiscard]] virtual RepositoryResult<User> find_by_id(
        ITransactionContext& tx,
        UserId id) = 0;
    [[nodiscard]] virtual RepositoryResult<User> find_by_username(
        ITransactionContext& tx,
        const std::string& username) = 0;

    /// @brief Create a new user. Returns the assigned UserId.
    [[nodiscard]] virtual RepositoryResult<UserId> create(
        ITransactionContext& tx,
        const std::string& username,
        const std::string& password_hash,
        const Currency& base_currency) = 0;

    [[nodiscard]] virtual RepositoryVoidResult save(
        ITransactionContext& tx,
        const User& user) = 0;
};

} // namespace pfh::domain
