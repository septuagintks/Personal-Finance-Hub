// Personal Finance Hub - PostgreSQL Account Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/account_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/balance_calculation_service.h"
#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

namespace {

constexpr const char* kAccountColumns = R"SQL(
    id, user_id, name, type::text, subtype, category::text, currency_code,
    description, is_archived, archived_at, created_at, updated_at, version
)SQL";

constexpr const char* kTransactionColumns = R"SQL(
    id, user_id, account_id, category_id, type::text, amount::text,
    currency_code, description, transfer_group_id, deleted_at,
    transaction_time, created_at
)SQL";

domain::RepositoryResult<domain::Account> map_account_row(
    const drogon::orm::Row& row) {
    try {
        const auto currency_code = pg::getString(row, 6);
        const auto currency = domain::Currency::create(currency_code);
        if (!currency.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Stored account has an unsupported currency"));
        }

        return domain::Account(
            domain::AccountId(pg::getBigInt(row, 0)),
            domain::UserId(pg::getBigInt(row, 1)),
            pg::getString(row, 2),
            pg::parseAccountType(pg::getString(row, 3)),
            pg::getString(row, 4),
            *currency,
            pg::getOptionalString(row, 7).value_or(""),
            pg::getBool(row, 8),
            pg::getOptionalTimestamp(row, 9),
            pg::getTimestamp(row, 10),
            pg::getTimestamp(row, 11),
            pg::getBigInt(row, 12),
            pg::parseAccountCategory(pg::getString(row, 5)));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored account row is invalid"));
    }
}

domain::RepositoryResult<domain::Transaction> map_transaction_row(
    const drogon::orm::Row& row) {
    try {
        const auto amount = domain::Decimal::parse_numeric_20_8(
            pg::getNumericAsString(row, 5));
        const auto currency = domain::Currency::create(pg::getString(row, 6));
        if (!amount.has_value() || !currency.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Stored transaction amount or currency is invalid"));
        }

        const auto category_value = pg::getOptionalBigInt(row, 3);
        const auto group_value = pg::getOptionalBigInt(row, 8);
        std::optional<domain::CategoryId> category_id;
        std::optional<domain::TransferGroupId> group_id;
        if (category_value.has_value()) {
            category_id = domain::CategoryId(*category_value);
        }
        if (group_value.has_value()) {
            group_id = domain::TransferGroupId(*group_value);
        }

        return domain::Transaction(
            domain::TransactionId(pg::getBigInt(row, 0)),
            domain::UserId(pg::getBigInt(row, 1)),
            domain::AccountId(pg::getBigInt(row, 2)),
            domain::Money(*amount, *currency),
            pg::parseTransactionType(pg::getString(row, 4)),
            pg::getTimestamp(row, 10),
            pg::getOptionalString(row, 7).value_or(""),
            category_id,
            group_id,
            pg::getTimestamp(row, 11),
            pg::getOptionalTimestamp(row, 9));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored transaction row is invalid"));
    }
}

domain::RepositoryError account_not_found() {
    return domain::RepositoryError::not_found("Account not found for user");
}

bool is_lock_conflict(const drogon::orm::DrogonDbException& error) {
    const std::string detail = error.base().what();
    return detail.find("55P03") != std::string::npos ||
           detail.find("could not obtain lock on row") != std::string::npos;
}

}  // namespace

domain::RepositoryResult<domain::Account> AccountRepositoryImpl::find_by_id(
    domain::AccountId id) {
    return postgres::execute_tenant_read<domain::Account>(
        db_, tenant_user_id_, "find account", [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") + kAccountColumns +
                " FROM accounts WHERE id = $1 AND user_id = $2";
            const auto result = transaction->execSqlSync(
                sql, id.value(), tenant_user_id_.value());
            if (result.empty()) {
                return domain::RepositoryResult<domain::Account>(
                    std::unexpected(account_not_found()));
            }
            return map_account_row(result[0]);
        });
}

domain::RepositoryResult<domain::Account>
AccountRepositoryImpl::find_by_id_for_user(
    domain::AccountId id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(account_not_found());
    }
    return find_by_id(id);
}

domain::RepositoryResult<domain::Account>
AccountRepositoryImpl::find_by_id_for_update(
    domain::ITransactionContext& tx_iface,
    domain::AccountId id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(account_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }

    const std::string sql = std::string("SELECT ") + kAccountColumns +
                            " FROM accounts WHERE id = $1 AND user_id = $2 "
                            "FOR UPDATE NOWAIT";
    try {
        const auto result = (*context)->transaction().execSqlSync(
            sql, id.value(), user_id.value());
        if (result.empty()) {
            return std::unexpected(account_not_found());
        }
        return map_account_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& error) {
        if (is_lock_conflict(error)) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Account is being changed by another request"));
        }
        return std::unexpected(postgres::database_error("lock account", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("lock account", error));
    }
}

domain::RepositoryResult<std::vector<domain::Account>>
AccountRepositoryImpl::find_active_by_user(domain::UserId user_id) {
    return find_by_user(user_id, false);
}

domain::RepositoryResult<std::vector<domain::Account>>
AccountRepositoryImpl::find_by_user(
    domain::UserId user_id,
    std::optional<bool> archived) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(account_not_found());
    }
    return postgres::execute_tenant_read<std::vector<domain::Account>>(
        db_, tenant_user_id_, "list accounts", [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") + kAccountColumns +
                " FROM accounts WHERE user_id = $1" +
                (archived.has_value() ? " AND is_archived = $2" : "") +
                " ORDER BY id";
            const auto result = archived.has_value()
                ? transaction->execSqlSync(sql, user_id.value(), *archived)
                : transaction->execSqlSync(sql, user_id.value());
            std::vector<domain::Account> accounts;
            accounts.reserve(result.size());
            for (const auto& row : result) {
                auto account = map_account_row(row);
                if (!account.has_value()) {
                    return domain::RepositoryResult<std::vector<domain::Account>>(
                        std::unexpected(account.error()));
                }
                accounts.push_back(std::move(*account));
            }
            return domain::RepositoryResult<std::vector<domain::Account>>(
                std::move(accounts));
        });
}

domain::RepositoryResult<bool> AccountRepositoryImpl::has_transactions(
    domain::ITransactionContext& tx_iface,
    domain::AccountId id) {
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) return std::unexpected(context.error());
    try {
        const auto result = (*context)->transaction().execSqlSync(
            "SELECT 1 FROM transactions "
            "WHERE account_id = $1 AND user_id = $2 LIMIT 1",
            id.value(), tenant_user_id_.value());
        return !result.empty();
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "check account history", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "check account history", error));
    }
}

domain::RepositoryResult<domain::BalanceSnapshot>
AccountRepositoryImpl::balance_of(domain::AccountId id) {
    return postgres::execute_transaction<domain::BalanceSnapshot>(
        db_, tenant_user_id_, "read account balance", [&](const auto& transaction) {
            const std::string account_sql = std::string("SELECT ") +
                kAccountColumns +
                " FROM accounts WHERE id = $1 AND user_id = $2 FOR UPDATE";
            const auto account_result = transaction->execSqlSync(
                account_sql, id.value(), tenant_user_id_.value());
            if (account_result.empty()) {
                return domain::RepositoryResult<domain::BalanceSnapshot>(
                    std::unexpected(account_not_found()));
            }
            auto account = map_account_row(account_result[0]);
            if (!account.has_value()) {
                return domain::RepositoryResult<domain::BalanceSnapshot>(
                    std::unexpected(account.error()));
            }

            constexpr const char* kVersionSql = R"SQL(
                SELECT COALESCE(MAX(version), 0), MAX(id)
                FROM transactions
                WHERE account_id = $1 AND user_id = $2 AND deleted_at IS NULL
            )SQL";
            const auto version_result = transaction->execSqlSync(
                kVersionSql, id.value(), tenant_user_id_.value());
            const auto source_version = pg::getBigInt(version_result[0], 0);
            const auto last_transaction_value =
                pg::getOptionalBigInt(version_result[0], 1);

            constexpr const char* kCacheSql = R"SQL(
                SELECT balance::text, last_transaction_id, source_version,
                       updated_at
                FROM account_balance_cache
                WHERE account_id = $1 AND user_id = $2
            )SQL";
            const auto cache_result = transaction->execSqlSync(
                kCacheSql, id.value(), tenant_user_id_.value());
            if (!cache_result.empty() &&
                pg::getBigInt(cache_result[0], 2) == source_version &&
                pg::getOptionalBigInt(cache_result[0], 1) ==
                    last_transaction_value) {
                auto balance = domain::Decimal::parse_numeric_20_8(
                    pg::getNumericAsString(cache_result[0], 0));
                if (!balance.has_value()) {
                    return domain::RepositoryResult<domain::BalanceSnapshot>(
                        std::unexpected(domain::RepositoryError::database(
                            "Stored balance cache is invalid")));
                }
                const domain::TransactionId last_transaction_id(
                    last_transaction_value.value_or(0));
                return domain::RepositoryResult<domain::BalanceSnapshot>(
                    domain::BalanceSnapshot(
                        id,
                        domain::Money(*balance, account->currency()),
                        pg::getTimestamp(cache_result[0], 3),
                        last_transaction_id));
            }

            const std::string transaction_sql = std::string("SELECT ") +
                kTransactionColumns +
                " FROM transactions WHERE account_id = $1 AND user_id = $2 "
                "AND deleted_at IS NULL ORDER BY transaction_time, id";
            const auto transaction_result = transaction->execSqlSync(
                transaction_sql, id.value(), tenant_user_id_.value());
            std::vector<domain::Transaction> transactions;
            transactions.reserve(transaction_result.size());
            for (const auto& row : transaction_result) {
                auto mapped = map_transaction_row(row);
                if (!mapped.has_value()) {
                    return domain::RepositoryResult<domain::BalanceSnapshot>(
                        std::unexpected(mapped.error()));
                }
                transactions.push_back(std::move(*mapped));
            }

            auto snapshot = domain::BalanceCalculationService::calculate_balance(
                id, transactions, account->currency());
            if (!snapshot.has_value()) {
                return domain::RepositoryResult<domain::BalanceSnapshot>(
                    std::unexpected(domain::RepositoryError::database(
                        "Balance calculation failed")));
            }
            snapshot->last_transaction_id =
                domain::TransactionId(last_transaction_value.value_or(0));

            constexpr const char* kUpsertCacheSql = R"SQL(
                INSERT INTO account_balance_cache (
                    account_id, user_id, balance, last_transaction_id,
                    source_version, cache_version, updated_at)
                VALUES ($1, $2, $3::numeric(20,8), $4, $5, 1, NOW())
                ON CONFLICT (account_id) DO UPDATE SET
                    user_id = EXCLUDED.user_id,
                    balance = EXCLUDED.balance,
                    last_transaction_id = EXCLUDED.last_transaction_id,
                    source_version = EXCLUDED.source_version,
                    cache_version = account_balance_cache.cache_version + 1,
                    updated_at = NOW()
            )SQL";
            transaction->execSqlSync(
                kUpsertCacheSql,
                id.value(),
                tenant_user_id_.value(),
                snapshot->balance.amount().to_string(),
                last_transaction_value,
                source_version);
            return domain::RepositoryResult<domain::BalanceSnapshot>(*snapshot);
        });
}

domain::RepositoryResult<domain::AccountId> AccountRepositoryImpl::save(
    domain::ITransactionContext& tx_iface,
    const domain::Account& account) {
    if (account.owner() != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::validation(
            "Account owner does not match repository tenant"));
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    auto& transaction = (*context)->transaction();

    try {
        const auto owner_result = transaction.execSqlSync(
            "SELECT 1 FROM users WHERE id = $1",
            account.owner().value());
        if (owner_result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Account owner not found"));
        }

        if (!account.id().is_valid()) {
            constexpr const char* kInsertSql = R"SQL(
                INSERT INTO accounts (
                    user_id, name, type, subtype, category, currency_code,
                    description, is_archived, archived_at, created_at, updated_at)
                VALUES (
                    $1, $2, $3::account_type, $4, $5::account_category, $6,
                    $7, $8, $9, $10, $11)
                RETURNING id
            )SQL";
            const auto result = transaction.execSqlSync(
                kInsertSql,
                account.owner().value(),
                account.name(),
                pg::toSqlText(account.type()),
                account.subtype(),
                pg::toSqlText(account.category()),
                account.currency().code(),
                account.description(),
                account.is_archived(),
                pg::toDbTimestamp(account.archived_at()),
                pg::toDbTimestamp(account.created_at()),
                pg::toDbTimestamp(account.updated_at()));
            if (result.empty()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Account insert returned no identifier"));
            }
            return domain::AccountId(pg::getBigInt(result[0], 0));
        }

        constexpr const char* kUpdateSql = R"SQL(
            UPDATE accounts SET
                name = $1, type = $2::account_type, subtype = $3,
                category = $4::account_category, currency_code = $5,
                description = $6, is_archived = $7, archived_at = $8,
                updated_at = $9, version = version + 1
            WHERE id = $10 AND user_id = $11 AND version = $12
        )SQL";
        const auto result = transaction.execSqlSync(
            kUpdateSql,
            account.name(),
            pg::toSqlText(account.type()),
            account.subtype(),
            pg::toSqlText(account.category()),
            account.currency().code(),
            account.description(),
            account.is_archived(),
            pg::toDbTimestamp(account.archived_at()),
            pg::toDbTimestamp(account.updated_at()),
            account.id().value(),
            account.owner().value(),
            account.version());
        if (result.affectedRows() == 0) {
            constexpr const char* kExistsSql =
                "SELECT 1 FROM accounts WHERE id = $1 AND user_id = $2";
            const auto existing = transaction.execSqlSync(
                kExistsSql, account.id().value(), account.owner().value());
            if (existing.empty()) {
                return std::unexpected(account_not_found());
            }
            return std::unexpected(domain::RepositoryError::conflict(
                "Account version conflict"));
        }

        transaction.execSqlSync(
            "DELETE FROM account_balance_cache "
            "WHERE account_id = $1 AND user_id = $2",
            account.id().value(),
            tenant_user_id_.value());
        return account.id();
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error("save account", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("save account", error));
    }
}

domain::RepositoryVoidResult AccountRepositoryImpl::delete_balance_cache(
    domain::ITransactionContext& tx_iface,
    domain::AccountId id) {
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    try {
        (*context)->transaction().execSqlSync(
            "DELETE FROM account_balance_cache "
            "WHERE account_id = $1 AND user_id = $2",
            id.value(),
            tenant_user_id_.value());
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("delete balance cache", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("delete balance cache", error));
    }
}

domain::RepositoryVoidResult AccountRepositoryImpl::physical_delete(
    domain::ITransactionContext& tx_iface,
    domain::AccountId id) {
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    try {
        const auto result = (*context)->transaction().execSqlSync(
            "DELETE FROM accounts WHERE id = $1 AND user_id = $2",
            id.value(),
            tenant_user_id_.value());
        if (result.affectedRows() == 0) {
            return std::unexpected(account_not_found());
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error("delete account", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("delete account", error));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
