// Personal Finance Hub - PostgreSQL User Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/user_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/repositories/repository_error.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

namespace pfh::infrastructure {

domain::RepositoryResult<domain::User> UserRepositoryImpl::find_by_id(
    domain::UserId id) {
    constexpr const char* kSql = R"SQL(
        SELECT id, username FROM users WHERE id = $1
    )SQL";

    try {
        auto result = db_->execSqlSync(kSql, id.value);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "User not found: " + id.to_string()));
        }
        const auto& row = result[0];
        const auto uid = domain::UserId(pg::getBigInt(row, 0));
        const auto username = pg::getString(row, 1);
        return domain::User(uid, username);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_id failed: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::User> UserRepositoryImpl::find_by_username(
    const std::string& username) {
    constexpr const char* kSql = R"SQL(
        SELECT id, username FROM users WHERE username = $1
    )SQL";

    try {
        auto result = db_->execSqlSync(kSql, username);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "User not found by username: " + username));
        }
        const auto& row = result[0];
        const auto id = domain::UserId(pg::getBigInt(row, 0));
        const auto uname = pg::getString(row, 1);
        return domain::User(id, uname);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_username failed: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::UserId> UserRepositoryImpl::create(
    domain::ITransactionContext& tx_iface,
    const std::string& username,
    const std::string& password_hash,
    const domain::Currency& base_currency) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    constexpr const char* kSql = R"SQL(
        INSERT INTO users (username, password_hash, base_currency_code)
        VALUES ($1, $2, $3)
        RETURNING id
    )SQL";

    try {
        auto result = tx.execSqlSync(kSql, username, password_hash, base_currency.code());
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "create user: no id returned"));
        }
        const auto id = domain::UserId(pg::getBigInt(result[0], 0));
        return id;
    } catch (const drogon::orm::DrogonDbException& e) {
        const std::string msg = e.base().what();
        // PostgreSQL unique violation error code is 23505.
        if (msg.find("23505") != std::string::npos ||
            msg.find("uq_users_username") != std::string::npos) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Username already exists: " + username));
        }
        return std::unexpected(domain::RepositoryError::database(
            std::string("create user failed: ") + msg));
    }
}

domain::RepositoryVoidResult UserRepositoryImpl::save(
    domain::ITransactionContext& tx_iface,
    const domain::User& user) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    // User entity only holds id + username; password_hash/base_currency_code
    // are managed elsewhere. Only username is updateable here (rare case).
    constexpr const char* kSql = R"SQL(
        UPDATE users SET username = $1, updated_at = NOW()
        WHERE id = $2
    )SQL";

    try {
        auto result = tx.execSqlSync(kSql, user.username(), user.id().value);
        if (result.affectedRows() == 0) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Cannot save unknown user: " + user.id().to_string()));
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("save user failed: ") + e.base().what()));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
