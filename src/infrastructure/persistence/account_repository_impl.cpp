// Personal Finance Hub - PostgreSQL Account Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/account_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/balance_calculation_service.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

namespace pfh::infrastructure {

namespace {

domain::RepositoryResult<domain::Account> map_account_row(
    const drogon::orm::Row& row) {
    try {
        const auto id = domain::AccountId(pg::getBigInt(row, 0));
        const auto user_id = domain::UserId(pg::getBigInt(row, 1));
        const auto name = pg::getString(row, 2);
        const auto type = pg::parseAccountType(pg::getString(row, 3));
        const auto subtype = pg::getString(row, 4);
        const auto category = pg::parseAccountCategory(pg::getString(row, 5));
        const auto currency_code = pg::getString(row, 6);
        const auto currency = domain::Currency::create(currency_code);
        if (!currency.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Unknown currency: " + currency_code));
        }
        const auto description = pg::getOptionalString(row, 7).value_or("");
        const auto is_archived = pg::getBool(row, 8);
        const auto archived_at = pg::getOptionalTimestamp(row, 9);
        const auto created_at = pg::getTimestamp(row, 10);
        const auto updated_at = pg::getTimestamp(row, 11);
        const auto version = pg::getBigInt(row, 12);

        return domain::Account(
            id, user_id, name, type, subtype, *currency, description,
            is_archived, archived_at, created_at, updated_at, version, category);
    } catch (const std::exception& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("map_account_row: ") + e.what()));
    }
}

constexpr const char* kSelectColumns = R"SQL(
    id, user_id, name, type::text, subtype, category::text, currency_code,
    description, is_archived, archived_at, created_at, updated_at, version
)SQL";

}  // namespace

domain::RepositoryResult<domain::Account> AccountRepositoryImpl::find_by_id(
    domain::AccountId id) {
    const std::string sql =
        std::string("SELECT ") + kSelectColumns + " FROM accounts WHERE id = $1";
    try {
        auto result = db_->execSqlSync(sql, id.value);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Account not found: " + id.to_string()));
        }
        return map_account_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_id: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::Account> AccountRepositoryImpl::find_by_id_for_user(
    domain::AccountId id,
    domain::UserId user_id) {
    const std::string sql =
        std::string("SELECT ") + kSelectColumns +
        " FROM accounts WHERE id = $1 AND user_id = $2";
    try {
        auto result = db_->execSqlSync(sql, id.value, user_id.value);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Account not found for user"));
        }
        return map_account_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_id_for_user: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::Account> AccountRepositoryImpl::find_by_id_for_update(
    domain::ITransactionContext& tx_iface,
    domain::AccountId id,
    domain::UserId user_id) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    const std::string sql =
        std::string("SELECT ") + kSelectColumns +
        " FROM accounts WHERE id = $1 AND user_id = $2 FOR UPDATE";
    try {
        auto result = tx.execSqlSync(sql, id.value, user_id.value);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Account not found for user"));
        }
        return map_account_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_id_for_update: ") + e.base().what()));
    }
}

domain::RepositoryResult<std::vector<domain::Account>>
AccountRepositoryImpl::find_active_by_user(domain::UserId user_id) {
    const std::string sql =
        std::string("SELECT ") + kSelectColumns +
        " FROM accounts WHERE user_id = $1 AND is_archived = FALSE ORDER BY id";
    try {
        auto result = db_->execSqlSync(sql, user_id.value);
        std::vector<domain::Account> accounts;
        accounts.reserve(result.size());
        for (const auto& row : result) {
            auto acc = map_account_row(row);
            if (!acc.has_value()) {
                return std::unexpected(acc.error());
            }
            accounts.push_back(std::move(*acc));
        }
        return accounts;
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_active_by_user: ") + e.base().what()));
    }
}

domain::RepositoryResult<std::vector<domain::Currency>>
AccountRepositoryImpl::find_active_currencies() {
    constexpr const char* kSql = R"SQL(
        SELECT DISTINCT currency_code FROM accounts
        WHERE is_archived = FALSE
        ORDER BY currency_code
    )SQL";
    try {
        auto result = db_->execSqlSync(kSql);
        std::vector<domain::Currency> currencies;
        currencies.reserve(result.size());
        for (const auto& row : result) {
            const auto code = pg::getString(row, 0);
            auto curr = domain::Currency::create(code);
            if (!curr.has_value()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Unknown currency: " + code));
            }
            currencies.push_back(*curr);
        }
        return currencies;
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_active_currencies: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::BalanceSnapshot> AccountRepositoryImpl::balance_of(
    domain::AccountId id) {
    // Implementation note: balance cache is optional optimization for Phase 1.
    // We always rebuild via domain service for simplicity; PostgreSQL implementation
    // can cache later (tasks §4.3 deferred optimization).
    auto account = find_by_id(id);
    if (!account.has_value()) {
        return std::unexpected(account.error());
    }

    constexpr const char* kTxSql = R"SQL(
        SELECT id, user_id, account_id, category_id, type::text,
               amount::text, currency_code, description, transfer_group_id,
               deleted_at, transaction_time, created_at
        FROM transactions
        WHERE account_id = $1 AND deleted_at IS NULL
        ORDER BY transaction_time, id
    )SQL";

    try {
        auto result = db_->execSqlSync(kTxSql, id.value);
        std::vector<domain::Transaction> txs;
        txs.reserve(result.size());

        for (const auto& row : result) {
            const auto tx_id = domain::TransactionId(pg::getBigInt(row, 0));
            const auto user_id = domain::UserId(pg::getBigInt(row, 1));
            const auto account_id = domain::AccountId(pg::getBigInt(row, 2));
            const auto category_id = pg::getOptionalBigInt(row, 3);
            const auto type = pg::parseTransactionType(pg::getString(row, 4));
            const auto amount_str = pg::getNumericAsString(row, 5);
            const auto currency_code = pg::getString(row, 6);
            const auto description = pg::getOptionalString(row, 7).value_or("");
            const auto transfer_group_id = pg::getOptionalBigInt(row, 8);
            const auto deleted_at = pg::getOptionalTimestamp(row, 9);
            const auto occurred_at = pg::getTimestamp(row, 10);
            const auto created_at = pg::getTimestamp(row, 11);

            auto amount_dec = domain::Decimal::parse_numeric_20_8(amount_str);
            if (!amount_dec.has_value()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Invalid amount: " + amount_str));
            }
            auto currency = domain::Currency::create(currency_code);
            if (!currency.has_value()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Unknown currency: " + currency_code));
            }
            domain::Money amount(*amount_dec, *currency);

            std::optional<domain::CategoryId> cat_opt;
            if (category_id.has_value()) {
                cat_opt = domain::CategoryId(*category_id);
            }
            std::optional<domain::TransferGroupId> tg_opt;
            if (transfer_group_id.has_value()) {
                tg_opt = domain::TransferGroupId(*transfer_group_id);
            }

            txs.emplace_back(
                tx_id, user_id, account_id, amount, type, occurred_at,
                description, cat_opt, tg_opt, created_at, deleted_at);
        }

        auto snapshot = domain::BalanceCalculationService::calculate_balance(
            id, txs, account->currency());
        if (!snapshot.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Balance calculation failed: " + snapshot.error().message));
        }
        if (!txs.empty()) {
            snapshot->last_transaction_id = txs.back().id();
        }
        return *snapshot;
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("balance_of: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::AccountId> AccountRepositoryImpl::save(
    domain::ITransactionContext& tx_iface,
    const domain::Account& account) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    if (!account.id().is_valid()) {
        // Insert path.
        constexpr const char* kInsertSql = R"SQL(
            INSERT INTO accounts (
                user_id, name, type, subtype, category, currency_code,
                description, is_archived, archived_at, created_at, updated_at)
            VALUES (
                $1, $2, $3::account_type, $4, $5::account_category, $6,
                $7, $8, $9, $10, $11)
            RETURNING id
        )SQL";
        try {
            auto result = tx.execSqlSync(
                kInsertSql,
                account.owner().value,
                account.name(),
                pg::toSqlText(account.type()),
                account.subtype(),
                pg::toSqlText(account.category()),
                account.currency().code(),
                account.description(),
                account.is_archived(),
                account.archived_at(),
                account.created_at(),
                account.updated_at());
            if (result.empty()) {
                return std::unexpected(domain::RepositoryError::database(
                    "insert account: no id returned"));
            }
            return domain::AccountId(pg::getBigInt(result[0], 0));
        } catch (const drogon::orm::DrogonDbException& e) {
            return std::unexpected(domain::RepositoryError::database(
                std::string("insert account: ") + e.base().what()));
        }
    }

    // Update path with optimistic lock.
    constexpr const char* kUpdateSql = R"SQL(
        UPDATE accounts SET
            name = $1, type = $2::account_type, subtype = $3,
            category = $4::account_category, currency_code = $5,
            description = $6, is_archived = $7, archived_at = $8,
            updated_at = $9, version = version + 1
        WHERE id = $10 AND user_id = $11 AND version = $12
    )SQL";
    try {
        auto result = tx.execSqlSync(
            kUpdateSql,
            account.name(),
            pg::toSqlText(account.type()),
            account.subtype(),
            pg::toSqlText(account.category()),
            account.currency().code(),
            account.description(),
            account.is_archived(),
            account.archived_at(),
            account.updated_at(),
            account.id().value,
            account.owner().value,
            account.version());
        if (result.affectedRows() == 0) {
            // Either account doesn't exist, or version mismatch.
            auto check = find_by_id_for_user(account.id(), account.owner());
            if (!check.has_value()) {
                return std::unexpected(domain::RepositoryError::not_found(
                    "Account not found: " + account.id().to_string()));
            }
            return std::unexpected(domain::RepositoryError::conflict(
                "Optimistic lock: version mismatch"));
        }
        return account.id();
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("update account: ") + e.base().what()));
    }
}

domain::RepositoryVoidResult AccountRepositoryImpl::delete_balance_cache(
    domain::ITransactionContext& tx_iface,
    domain::AccountId id) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    constexpr const char* kSql = "DELETE FROM account_balance_cache WHERE account_id = $1";
    try {
        tx.execSqlSync(kSql, id.value);
        return {};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("delete_balance_cache: ") + e.base().what()));
    }
}

domain::RepositoryVoidResult AccountRepositoryImpl::physical_delete(
    domain::ITransactionContext& tx_iface,
    domain::AccountId id) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    constexpr const char* kSql = "DELETE FROM accounts WHERE id = $1";
    try {
        auto result = tx.execSqlSync(kSql, id.value);
        if (result.affectedRows() == 0) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Account not found: " + id.to_string()));
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("physical_delete: ") + e.base().what()));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
