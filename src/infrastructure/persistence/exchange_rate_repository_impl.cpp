// Personal Finance Hub - PostgreSQL ExchangeRate Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/exchange_rate_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/repositories/repository_error.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

namespace pfh::infrastructure {

namespace {

domain::RepositoryResult<domain::ExchangeRate> map_exchange_rate_row(
    const drogon::orm::Row& row) {
    try {
        const auto base_code = pg::getString(row, 0);
        const auto target_code = pg::getString(row, 1);
        const auto rate_str = pg::getNumericAsString(row, 2);
        const auto source = pg::getString(row, 3);
        const auto fetched_at = pg::getTimestamp(row, 4);

        auto base = domain::Currency::create(base_code);
        if (!base.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Unknown base currency: " + base_code));
        }
        auto target = domain::Currency::create(target_code);
        if (!target.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Unknown target currency: " + target_code));
        }
        auto rate_dec = domain::Decimal::parse_numeric_20_10(rate_str);
        if (!rate_dec.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Invalid rate: " + rate_str));
        }
        auto rate = domain::ExchangeRate::create(
            *base, *target, *rate_dec, fetched_at, source);
        if (!rate.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Invalid exchange rate: " + rate.error().message));
        }
        return *rate;
    } catch (const std::exception& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("map_exchange_rate_row: ") + e.what()));
    }
}

}  // namespace

domain::RepositoryResult<domain::ExchangeRateId>
ExchangeRateRepositoryImpl::append(
    domain::ITransactionContext& tx_iface,
    const domain::ExchangeRate& rate) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    constexpr const char* kSql = R"SQL(
        INSERT INTO exchange_rates (
            base_currency_code, target_currency_code, rate, source, fetched_at)
        VALUES ($1, $2, $3::numeric(20,10), $4, $5)
        RETURNING id
    )SQL";

    try {
        auto result = tx.execSqlSync(
            kSql,
            rate.base().code(),
            rate.target().code(),
            rate.rate().to_string(),
            rate.source(),
            rate.fetched_at());
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "append exchange rate: no id returned"));
        }
        return domain::ExchangeRateId(pg::getBigInt(result[0], 0));
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("append exchange rate: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::ExchangeRate>
ExchangeRateRepositoryImpl::find_latest(
    const domain::Currency& base,
    const domain::Currency& target) {
    constexpr const char* kSql = R"SQL(
        SELECT base_currency_code, target_currency_code, rate::text, source, fetched_at
        FROM exchange_rates
        WHERE base_currency_code = $1 AND target_currency_code = $2
        ORDER BY fetched_at DESC, id DESC
        LIMIT 1
    )SQL";

    try {
        auto result = db_->execSqlSync(kSql, base.code(), target.code());
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "No exchange rate found for " + base.code() + "->" + target.code()));
        }
        return map_exchange_rate_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_latest: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::ExchangeRate>
ExchangeRateRepositoryImpl::find_historical(
    const domain::Currency& base,
    const domain::Currency& target,
    std::chrono::system_clock::time_point target_time) {
    constexpr const char* kSql = R"SQL(
        SELECT base_currency_code, target_currency_code, rate::text, source, fetched_at
        FROM exchange_rates
        WHERE base_currency_code = $1 AND target_currency_code = $2
          AND fetched_at <= $3
        ORDER BY fetched_at DESC, id DESC
        LIMIT 1
    )SQL";

    try {
        auto result = db_->execSqlSync(kSql, base.code(), target.code(), target_time);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "No historical exchange rate found for " + base.code() +
                "->" + target.code() + " at target time"));
        }
        return map_exchange_rate_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_historical: ") + e.base().what()));
    }
}

domain::RepositoryResult<std::vector<domain::ExchangeRate>>
ExchangeRateRepositoryImpl::find_all_for_pair(
    const domain::Currency& base,
    const domain::Currency& target) {
    constexpr const char* kSql = R"SQL(
        SELECT base_currency_code, target_currency_code, rate::text, source, fetched_at
        FROM exchange_rates
        WHERE base_currency_code = $1 AND target_currency_code = $2
        ORDER BY fetched_at, id
    )SQL";

    try {
        auto result = db_->execSqlSync(kSql, base.code(), target.code());
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
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_all_for_pair: ") + e.base().what()));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
