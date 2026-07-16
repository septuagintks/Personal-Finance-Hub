// Personal Finance Hub - PostgreSQL ExchangeRate Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/exchange_rate_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <utility>
#include <vector>

namespace pfh::infrastructure {

namespace {

domain::RepositoryResult<domain::ExchangeRate> map_exchange_rate_row(
    const drogon::orm::Row& row) {
    try {
        const auto base = domain::Currency::create(pg::getString(row, 0));
        const auto target = domain::Currency::create(pg::getString(row, 1));
        const auto rate = domain::Decimal::parse_numeric_20_10(
            pg::getNumericAsString(row, 2));
        if (!base.has_value() || !target.has_value() || !rate.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Stored exchange-rate row is invalid"));
        }
        auto mapped = domain::ExchangeRate::create(
            *base,
            *target,
            *rate,
            pg::getTimestamp(row, 4),
            pg::getString(row, 3));
        if (!mapped.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Stored exchange-rate row violates domain invariants"));
        }
        return *mapped;
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored exchange-rate row is invalid"));
    }
}

domain::RepositoryError rate_not_found() {
    return domain::RepositoryError::not_found("Exchange rate not found");
}

}  // namespace

domain::RepositoryResult<domain::ExchangeRateId>
ExchangeRateRepositoryImpl::append(
    domain::ITransactionContext& tx_iface,
    const domain::ExchangeRate& rate) {
    if (!rate.rate().fits_numeric_20_10()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Exchange rate does not fit NUMERIC(20,10)"));
    }
    auto context = postgres::require_transaction(tx_iface);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    try {
        constexpr const char* kSql = R"SQL(
            INSERT INTO exchange_rates (
                base_currency_code, target_currency_code, rate, source, fetched_at)
            VALUES ($1, $2, $3::numeric(20,10), $4, $5)
            RETURNING id
        )SQL";
        const auto result = (*context)->transaction().execSqlSync(
            kSql,
            rate.base().code(),
            rate.target().code(),
            rate.rate().to_string(),
            rate.source(),
            pg::toDbTimestamp(rate.fetched_at()));
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "Exchange-rate insert returned no identifier"));
        }
        return domain::ExchangeRateId(pg::getBigInt(result[0], 0));
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("append exchange rate", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("append exchange rate", error));
    }
}

domain::RepositoryResult<domain::ExchangeRate>
ExchangeRateRepositoryImpl::find_latest(
    const domain::Currency& base,
    const domain::Currency& target) {
    if (!db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Exchange-rate database client is unavailable"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            SELECT base_currency_code, target_currency_code, rate::text,
                   source, fetched_at
            FROM exchange_rates
            WHERE base_currency_code = $1 AND target_currency_code = $2
            ORDER BY fetched_at DESC, id DESC
            LIMIT 1
        )SQL";
        const auto result = db_->execSqlSync(kSql, base.code(), target.code());
        if (result.empty()) {
            return std::unexpected(rate_not_found());
        }
        return map_exchange_rate_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("find latest exchange rate", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("find latest exchange rate", error));
    }
}

domain::RepositoryResult<domain::ExchangeRate>
ExchangeRateRepositoryImpl::find_historical(
    const domain::Currency& base,
    const domain::Currency& target,
    std::chrono::system_clock::time_point target_time) {
    if (!db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Exchange-rate database client is unavailable"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            SELECT base_currency_code, target_currency_code, rate::text,
                   source, fetched_at
            FROM exchange_rates
            WHERE base_currency_code = $1 AND target_currency_code = $2
              AND fetched_at <= $3
            ORDER BY fetched_at DESC, id DESC
            LIMIT 1
        )SQL";
        const auto result = db_->execSqlSync(
            kSql, base.code(), target.code(), pg::toDbTimestamp(target_time));
        if (result.empty()) {
            return std::unexpected(rate_not_found());
        }
        return map_exchange_rate_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("find historical exchange rate", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("find historical exchange rate", error));
    }
}

domain::RepositoryResult<std::vector<domain::ExchangeRate>>
ExchangeRateRepositoryImpl::find_all_for_pair(
    const domain::Currency& base,
    const domain::Currency& target) {
    if (!db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Exchange-rate database client is unavailable"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            SELECT base_currency_code, target_currency_code, rate::text,
                   source, fetched_at
            FROM exchange_rates
            WHERE base_currency_code = $1 AND target_currency_code = $2
            ORDER BY fetched_at, id
        )SQL";
        const auto result = db_->execSqlSync(kSql, base.code(), target.code());
        std::vector<domain::ExchangeRate> rates;
        rates.reserve(result.size());
        for (const auto& row : result) {
            auto rate = map_exchange_rate_row(row);
            if (!rate.has_value()) {
                return std::unexpected(rate.error());
            }
            rates.push_back(std::move(*rate));
        }
        return rates;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("list exchange rates", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("list exchange rates", error));
    }
}

domain::RepositoryResult<std::vector<domain::ExchangeRate>>
ExchangeRateRepositoryImpl::find_history_for_pair(
    const domain::Currency& base,
    const domain::Currency& target,
    std::chrono::system_clock::time_point from,
    std::chrono::system_clock::time_point to) {
    if (!db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Exchange-rate database client is unavailable"));
    }
    if (to < from) {
        return std::unexpected(domain::RepositoryError::validation(
            "Exchange-rate history range is invalid"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            (SELECT base_currency_code, target_currency_code, rate::text,
                    source, fetched_at
             FROM exchange_rates
             WHERE base_currency_code = $1 AND target_currency_code = $2
               AND fetched_at <= $3
             ORDER BY fetched_at DESC, id DESC
             LIMIT 1)
            UNION ALL
            (SELECT DISTINCT ON (fetched_at)
                    base_currency_code, target_currency_code, rate::text,
                    source, fetched_at
             FROM exchange_rates
             WHERE base_currency_code = $1 AND target_currency_code = $2
               AND fetched_at > $3 AND fetched_at <= $4
             ORDER BY fetched_at, id DESC)
            ORDER BY fetched_at
        )SQL";
        const auto rows = db_->execSqlSync(
            kSql, base.code(), target.code(), pg::toDbTimestamp(from),
            pg::toDbTimestamp(to));
        std::vector<domain::ExchangeRate> rates;
        rates.reserve(rows.size());
        for (const auto& row : rows) {
            auto rate = map_exchange_rate_row(row);
            if (!rate) {
                return std::unexpected(rate.error());
            }
            rates.push_back(std::move(*rate));
        }
        return rates;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "load exchange-rate history", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "load exchange-rate history", error));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
