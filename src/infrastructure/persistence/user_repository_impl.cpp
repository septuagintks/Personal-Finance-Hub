// Personal Finance Hub - PostgreSQL User Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/user_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <string>

namespace pfh::infrastructure {

namespace {

bool is_username_conflict(const drogon::orm::DrogonDbException& error) {
    const std::string detail = error.base().what();
    return detail.find("23505") != std::string::npos ||
           detail.find("uq_users_username") != std::string::npos;
}

domain::RepositoryResult<domain::User> map_user_result(
    const drogon::orm::Result& result) {
    if (result.empty()) {
        return std::unexpected(domain::RepositoryError::not_found(
            "User not found"));
    }
    try {
        return domain::User(
            domain::UserId(pg::getBigInt(result[0], 0)),
            pg::getString(result[0], 1));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored user row is invalid"));
    }
}

}  // namespace

domain::RepositoryResult<domain::User> UserRepositoryImpl::find_by_id(
    domain::UserId id) {
    if (!db_) {
        return std::unexpected(domain::RepositoryError::database(
            "User database client is unavailable"));
    }
    try {
        constexpr const char* kSql =
            "SELECT id, username FROM users WHERE id = $1";
        return map_user_result(db_->execSqlSync(kSql, id.value()));
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error("find user", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("find user", error));
    }
}

domain::RepositoryResult<domain::User> UserRepositoryImpl::find_by_username(
    const std::string& username) {
    if (!db_) {
        return std::unexpected(domain::RepositoryError::database(
            "User database client is unavailable"));
    }
    try {
        constexpr const char* kSql =
            "SELECT id, username FROM users WHERE username = $1";
        return map_user_result(db_->execSqlSync(kSql, username));
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("find user by username", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("find user by username", error));
    }
}

domain::RepositoryResult<domain::User> UserRepositoryImpl::find_by_id(
    domain::ITransactionContext& tx_iface,
    domain::UserId id) {
    auto context = postgres::require_transaction(tx_iface);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    const auto tenant = (*context)->tenant_user_id();
    if (tenant.has_value() && *tenant != id) {
        return std::unexpected(domain::RepositoryError::not_found(
            "User not found"));
    }
    try {
        constexpr const char* kSql =
            "SELECT id, username FROM users WHERE id = $1";
        return map_user_result(
            (*context)->transaction().execSqlSync(kSql, id.value()));
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("find user in transaction", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("find user in transaction", error));
    }
}

domain::RepositoryResult<domain::User> UserRepositoryImpl::find_by_username(
    domain::ITransactionContext& tx_iface,
    const std::string& username) {
    auto context = postgres::require_transaction(tx_iface);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    try {
        constexpr const char* kSql =
            "SELECT id, username FROM users WHERE username = $1";
        auto user = map_user_result(
            (*context)->transaction().execSqlSync(kSql, username));
        if (!user.has_value()) {
            return user;
        }
        const auto tenant = (*context)->tenant_user_id();
        if (tenant.has_value() && *tenant != user->id()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "User not found"));
        }
        return user;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "find user by username in transaction", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "find user by username in transaction", error));
    }
}

domain::RepositoryResult<domain::UserId> UserRepositoryImpl::create(
    domain::ITransactionContext& tx_iface,
    const std::string& username,
    const std::string& password_hash,
    const domain::Currency& base_currency) {
    auto context = postgres::require_transaction(tx_iface);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    if ((*context)->tenant_user_id().has_value()) {
        return std::unexpected(domain::RepositoryError::validation(
            "User creation requires an unscoped registration transaction"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            INSERT INTO users (username, password_hash, base_currency_code)
            VALUES ($1, $2, $3)
            RETURNING id
        )SQL";
        const auto result = (*context)->transaction().execSqlSync(
            kSql, username, password_hash, base_currency.code());
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "User insert returned no identifier"));
        }
        return domain::UserId(pg::getBigInt(result[0], 0));
    } catch (const drogon::orm::DrogonDbException& error) {
        if (is_username_conflict(error)) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Username already exists"));
        }
        return std::unexpected(postgres::database_error("create user", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("create user", error));
    }
}

domain::RepositoryVoidResult UserRepositoryImpl::save(
    domain::ITransactionContext& tx_iface,
    const domain::User& user) {
    if (!user.id().is_valid()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Cannot update a user without an identifier"));
    }
    auto context = postgres::require_transaction(tx_iface);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    const auto tenant = (*context)->tenant_user_id();
    if (!tenant.has_value() || *tenant != user.id()) {
        return std::unexpected(domain::RepositoryError::not_found(
            "User not found"));
    }

    try {
        constexpr const char* kSql = R"SQL(
            UPDATE users SET username = $1, updated_at = NOW()
            WHERE id = $2
        )SQL";
        const auto result = (*context)->transaction().execSqlSync(
            kSql, user.username(), user.id().value());
        if (result.affectedRows() == 0) {
            return std::unexpected(domain::RepositoryError::not_found(
                "User not found"));
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        if (is_username_conflict(error)) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Username already exists"));
        }
        return std::unexpected(postgres::database_error("save user", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("save user", error));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
