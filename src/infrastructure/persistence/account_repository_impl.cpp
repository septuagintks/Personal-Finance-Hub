// Personal Finance Hub - PostgreSQL Account Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/account_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <nlohmann/json.hpp>

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

domain::RepositoryResult<domain::BalanceSnapshot> map_cached_balance(
    const drogon::orm::Row& row,
    domain::AccountId account_id) {
    try {
        const auto currency = domain::Currency::create(pg::getString(row, 0));
        const auto amount = domain::Decimal::parse_numeric_20_8(
            pg::getNumericAsString(row, 1));
        if (!currency.has_value() || !amount.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Stored balance cache is invalid"));
        }
        return domain::BalanceSnapshot(
            account_id,
            domain::Money(*amount, *currency),
            pg::getTimestamp(row, 3),
            domain::TransactionId(pg::getOptionalBigInt(row, 2).value_or(0)));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored balance cache row is invalid"));
    }
}

domain::RepositoryError account_not_found() {
    return domain::RepositoryError::not_found("Account not found for user");
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
        if (postgres::is_lock_conflict(error)) {
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
            constexpr const char* kFastPathSql = R"SQL(
                SELECT account.currency_code, cache.balance::text,
                       cache.last_transaction_id, cache.updated_at
                FROM accounts AS account
                LEFT JOIN account_balance_cache AS cache
                  ON cache.account_id = account.id
                 AND cache.user_id = account.user_id
                WHERE account.id = $1 AND account.user_id = $2
            )SQL";
            const auto fast_path = transaction->execSqlSync(
                kFastPathSql, id.value(), tenant_user_id_.value());
            if (fast_path.empty()) {
                return domain::RepositoryResult<domain::BalanceSnapshot>(
                    std::unexpected(account_not_found()));
            }
            if (pg::getOptionalString(fast_path[0], 1).has_value()) {
                return map_cached_balance(fast_path[0], id);
            }

            constexpr const char* kLockedRecheckSql = R"SQL(
                SELECT account.currency_code, cache.balance::text,
                       cache.last_transaction_id, cache.updated_at
                FROM accounts AS account
                LEFT JOIN account_balance_cache AS cache
                  ON cache.account_id = account.id
                 AND cache.user_id = account.user_id
                WHERE account.id = $1 AND account.user_id = $2
                FOR UPDATE OF account
            )SQL";
            const auto locked = transaction->execSqlSync(
                kLockedRecheckSql, id.value(), tenant_user_id_.value());
            if (locked.empty()) {
                return domain::RepositoryResult<domain::BalanceSnapshot>(
                    std::unexpected(account_not_found()));
            }
            if (pg::getOptionalString(locked[0], 1).has_value()) {
                return map_cached_balance(locked[0], id);
            }

            const auto currency = domain::Currency::create(
                pg::getString(locked[0], 0));
            if (!currency.has_value()) {
                return domain::RepositoryResult<domain::BalanceSnapshot>(
                    std::unexpected(domain::RepositoryError::database(
                        "Stored account has an unsupported currency")));
            }

            constexpr const char* kAggregateSql = R"SQL(
                SELECT COALESCE(SUM(amount), 0)::text,
                       COALESCE(MAX(version), 0), MAX(id)
                FROM transactions
                WHERE account_id = $1 AND user_id = $2
                  AND deleted_at IS NULL
            )SQL";
            const auto aggregate = transaction->execSqlSync(
                kAggregateSql, id.value(), tenant_user_id_.value());
            const auto balance = domain::Decimal::parse_numeric_20_8(
                pg::getNumericAsString(aggregate[0], 0));
            if (!balance.has_value()) {
                return domain::RepositoryResult<domain::BalanceSnapshot>(
                    std::unexpected(domain::RepositoryError::database(
                        "Aggregated account balance is invalid")));
            }
            const auto source_version = pg::getBigInt(aggregate[0], 1);
            const auto last_transaction_value =
                pg::getOptionalBigInt(aggregate[0], 2);

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
                RETURNING balance::text, last_transaction_id, updated_at
            )SQL";
            const auto rebuilt = transaction->execSqlSync(
                kUpsertCacheSql,
                id.value(),
                tenant_user_id_.value(),
                balance->to_string(),
                last_transaction_value,
                source_version);
            return domain::RepositoryResult<domain::BalanceSnapshot>(
                domain::BalanceSnapshot(
                    id,
                    domain::Money(*balance, *currency),
                    pg::getTimestamp(rebuilt[0], 2),
                    domain::TransactionId(
                        pg::getOptionalBigInt(rebuilt[0], 1).value_or(0))));
        });
}

domain::RepositoryResult<std::vector<domain::BalanceCacheRebuildResult>>
AccountRepositoryImpl::rebuild_balance_cache(
    domain::ITransactionContext& tx_iface,
    domain::UserId user_id,
    std::optional<domain::AccountId> account_id,
    std::chrono::system_clock::time_point rebuilt_at) {
    if (user_id != tenant_user_id_ ||
        (account_id.has_value() && !account_id->is_valid())) {
        return std::unexpected(domain::RepositoryError::validation(
            "Balance cache rebuild request is invalid"));
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) return std::unexpected(context.error());

    try {
        constexpr const char* kSql = R"SQL(
            WITH target_accounts AS MATERIALIZED (
                SELECT id, user_id, currency_code
                FROM accounts
                WHERE user_id = $1
                  AND ($2::bigint IS NULL OR id = $2)
                ORDER BY id
                FOR UPDATE
            ), aggregated AS (
                SELECT target.id AS account_id,
                       target.user_id,
                       target.currency_code,
                       COALESCE(SUM(entry.amount), 0)::text AS balance,
                       COALESCE(MAX(entry.version), 0) AS source_version,
                       MAX(entry.id) AS last_transaction_id
                FROM target_accounts AS target
                LEFT JOIN transactions AS entry
                  ON entry.account_id = target.id
                 AND entry.user_id = target.user_id
                 AND entry.deleted_at IS NULL
                GROUP BY target.id, target.user_id, target.currency_code
            ), upserted AS (
                INSERT INTO account_balance_cache (
                    account_id, user_id, balance, last_transaction_id,
                    source_version, cache_version, updated_at)
                SELECT account_id, user_id, balance::numeric(20,8),
                       last_transaction_id, source_version, 1, $3
                FROM aggregated
                ON CONFLICT (account_id) DO UPDATE SET
                    user_id = EXCLUDED.user_id,
                    balance = EXCLUDED.balance,
                    last_transaction_id = EXCLUDED.last_transaction_id,
                    source_version = EXCLUDED.source_version,
                    cache_version = account_balance_cache.cache_version + 1,
                    updated_at = EXCLUDED.updated_at
                RETURNING account_id, balance::text, last_transaction_id,
                          source_version, cache_version, updated_at
            )
            SELECT upserted.account_id, aggregated.currency_code,
                   upserted.balance, upserted.last_transaction_id,
                   upserted.source_version, upserted.cache_version,
                   upserted.updated_at
            FROM upserted
            JOIN aggregated USING (account_id)
            ORDER BY upserted.account_id
        )SQL";
        const std::optional<std::int64_t> raw_account_id =
            account_id.has_value()
                ? std::optional<std::int64_t>(account_id->value())
                : std::nullopt;
        const auto rows = (*context)->transaction().execSqlSync(
            kSql,
            user_id.value(),
            raw_account_id,
            pg::toDbTimestamp(rebuilt_at));
        if (account_id.has_value() && rows.empty()) {
            return std::unexpected(account_not_found());
        }

        std::vector<domain::BalanceCacheRebuildResult> results;
        results.reserve(rows.size());
        for (const auto& row : rows) {
            const domain::AccountId id(pg::getBigInt(row, 0));
            auto currency = domain::Currency::create(pg::getString(row, 1));
            auto amount = domain::Decimal::parse_numeric_20_8(
                pg::getNumericAsString(row, 2));
            if (!currency || !amount) {
                return std::unexpected(domain::RepositoryError::database(
                    "Rebuilt balance cache row is invalid"));
            }
            const auto last_id = pg::getOptionalBigInt(row, 3);
            const auto updated_at = pg::getTimestamp(row, 6);
            results.push_back(domain::BalanceCacheRebuildResult{
                domain::BalanceSnapshot(
                    id,
                    domain::Money(*amount, *currency),
                    updated_at,
                    domain::TransactionId(last_id.value_or(0))),
                pg::getBigInt(row, 4),
                pg::getBigInt(row, 5)});
        }
        return results;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "rebuild balance cache", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "rebuild balance cache", error));
    }
}

domain::RepositoryResult<std::vector<domain::AccountBalanceAt>>
AccountRepositoryImpl::balances_at(
    domain::UserId user_id,
    std::chrono::system_clock::time_point as_of) {
    auto projections = balances_at_many(user_id, {as_of});
    if (!projections) return std::unexpected(projections.error());
    return std::move(projections->front().balances);
}

domain::RepositoryResult<std::vector<domain::AccountBalancesAtPoint>>
AccountRepositoryImpl::balances_at_many(
    domain::UserId user_id,
    const std::vector<std::chrono::system_clock::time_point>& as_of) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(account_not_found());
    }
    if (as_of.size() > domain::kMaximumBalanceProjectionPoints) {
        return std::unexpected(domain::RepositoryError::validation(
            "Historical balance projection request is too large"));
    }
    if (as_of.empty()) {
        return std::vector<domain::AccountBalancesAtPoint>{};
    }

    nlohmann::json points = nlohmann::json::array();
    for (const auto point : as_of) {
        points.push_back(pg::toDbTimestamp(point));
    }
    return postgres::execute_tenant_read<
        std::vector<domain::AccountBalancesAtPoint>>(
        db_, tenant_user_id_, "read historical account balances",
        [&](const auto& transaction) {
            constexpr const char* kSql = R"SQL(
                WITH requested AS (
                    SELECT value::timestamptz AS as_of, position
                    FROM jsonb_array_elements_text($2::jsonb)
                         WITH ORDINALITY AS point(value, position)
                )
                SELECT
                    account.id, account.user_id, account.name,
                    account.type::text, account.subtype,
                    account.category::text, account.currency_code,
                    account.description, account.is_archived,
                    account.archived_at, account.created_at,
                    account.updated_at, account.version,
                    COALESCE(SUM(entry.amount), 0)::text,
                    requested.position
                FROM requested
                JOIN accounts account ON account.user_id = $1
                LEFT JOIN transactions entry
                  ON entry.account_id = account.id
                 AND entry.user_id = account.user_id
                 AND entry.deleted_at IS NULL
                 AND entry.transaction_time <= requested.as_of
                WHERE account.archived_at IS NULL
                   OR account.archived_at > requested.as_of
                GROUP BY
                    requested.position,
                    account.id, account.user_id, account.name, account.type,
                    account.subtype, account.category, account.currency_code,
                    account.description, account.is_archived,
                    account.archived_at, account.created_at,
                    account.updated_at, account.version
                ORDER BY requested.position, account.id
            )SQL";
            const auto rows = transaction->execSqlSync(
                kSql, user_id.value(), points.dump());
            std::vector<domain::AccountBalancesAtPoint> result;
            result.reserve(as_of.size());
            for (const auto point : as_of) {
                result.push_back(domain::AccountBalancesAtPoint{point, {}});
            }
            for (const auto& row : rows) {
                const auto position = pg::getBigInt(row, 14);
                if (position <= 0 ||
                    static_cast<std::size_t>(position) > result.size()) {
                    return domain::RepositoryResult<
                        std::vector<domain::AccountBalancesAtPoint>>(
                        std::unexpected(domain::RepositoryError::database(
                            "Historical balance projection position is invalid")));
                }
                auto account = map_account_row(row);
                if (!account) {
                    return domain::RepositoryResult<
                        std::vector<domain::AccountBalancesAtPoint>>(
                        std::unexpected(account.error()));
                }
                auto amount = domain::Decimal::parse_numeric_20_8(
                    pg::getNumericAsString(row, 13));
                if (!amount) {
                    return domain::RepositoryResult<
                        std::vector<domain::AccountBalancesAtPoint>>(
                        std::unexpected(domain::RepositoryError::database(
                            "Historical account balance is invalid")));
                }
                result[static_cast<std::size_t>(position - 1)].balances.push_back(
                    domain::AccountBalanceAt{
                    *account,
                    domain::Money(*amount, account->currency())});
            }
            return domain::RepositoryResult<
                std::vector<domain::AccountBalancesAtPoint>>(std::move(result));
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
