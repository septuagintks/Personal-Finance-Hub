// Personal Finance Hub - PostgreSQL ExchangeRate Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/exchange_rate_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

namespace {

domain::RepositoryResult<domain::ExchangeRate> map_exchange_rate_row(
    const drogon::orm::Row& row,
    std::size_t offset = 0) {
    try {
        const auto base = domain::Currency::create(pg::getString(row, offset));
        const auto target = domain::Currency::create(pg::getString(row, offset + 1U));
        const auto rate = domain::Decimal::parse_numeric_20_10(
            pg::getNumericAsString(row, offset + 2U));
        if (!base.has_value() || !target.has_value() || !rate.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Stored exchange-rate row is invalid"));
        }
        auto mapped = domain::ExchangeRate::create(
            *base,
            *target,
            *rate,
            pg::getTimestamp(row, offset + 4U),
            pg::getString(row, offset + 3U));
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

domain::RepositoryResult<
    std::vector<std::optional<domain::ExchangeRate>>>
ExchangeRateRepositoryImpl::find_historical_at_points(
    const std::vector<domain::HistoricalRatePoint>& points) {
    if (!db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Exchange-rate database client is unavailable"));
    }
    if (points.size() > domain::kMaximumHistoricalRatePointBatch) {
        return std::unexpected(domain::RepositoryError::resource_limit(
            "Historical rate point batch exceeds 1024 items"));
    }
    if (points.empty()) {
        return std::vector<std::optional<domain::ExchangeRate>>{};
    }

    try {
        nlohmann::json requested = nlohmann::json::array();
        for (const auto& point : points) {
            requested.push_back({
                {"base", point.base.code()},
                {"target", point.target.code()},
                {"at", pg::toDbTimestamp(point.at)}});
        }
        constexpr const char* kSql = R"SQL(
            SELECT requested.position,
                   rate.base_currency_code,
                   rate.target_currency_code,
                   rate.rate::text,
                   rate.source,
                   rate.fetched_at
            FROM jsonb_array_elements($1::jsonb) WITH ORDINALITY
                 AS requested(payload, position)
            LEFT JOIN LATERAL (
                SELECT base_currency_code, target_currency_code, rate,
                       source, fetched_at
                FROM exchange_rates
                WHERE base_currency_code = requested.payload->>'base'
                  AND target_currency_code = requested.payload->>'target'
                  AND fetched_at <=
                      (requested.payload->>'at')::timestamptz
                ORDER BY fetched_at DESC, id DESC
                LIMIT 1
            ) AS rate ON TRUE
            ORDER BY requested.position
        )SQL";
        const auto rows = db_->execSqlSync(kSql, requested.dump());
        if (rows.size() != points.size()) {
            return std::unexpected(domain::RepositoryError::database(
                "Historical rate point batch returned an invalid row count"));
        }
        std::vector<std::optional<domain::ExchangeRate>> result;
        result.reserve(rows.size());
        for (std::size_t index = 0; index < rows.size(); ++index) {
            if (pg::getBigInt(rows[index], 0) !=
                static_cast<std::int64_t>(index + 1U)) {
                return std::unexpected(domain::RepositoryError::database(
                    "Historical rate point batch order is invalid"));
            }
            if (rows[index][1].isNull()) {
                result.emplace_back(std::nullopt);
                continue;
            }
            auto mapped = map_exchange_rate_row(rows[index], 1U);
            if (!mapped) {
                return std::unexpected(mapped.error());
            }
            result.emplace_back(std::move(*mapped));
        }
        return result;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "load point-in-time exchange rates", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "load point-in-time exchange rates", error));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
