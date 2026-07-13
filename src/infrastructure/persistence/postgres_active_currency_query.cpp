// Personal Finance Hub - PostgreSQL Active Currency Query Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/postgres_active_currency_query.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <utility>

namespace pfh::infrastructure {

domain::RepositoryResult<std::vector<domain::Currency>>
PostgresActiveCurrencyQuery::list_active_currencies() {
    if (!privileged_read_db_) {
        return std::unexpected(domain::RepositoryError::database(
            "Active-currency database client is unavailable"));
    }
    try {
        constexpr const char* kSql = R"SQL(
            SELECT currency_code
            FROM (
                SELECT currency_code
                FROM accounts
                WHERE is_archived = FALSE
                UNION
                SELECT base_currency_code AS currency_code
                FROM users
            ) reporting_currencies
            ORDER BY currency_code
        )SQL";
        const auto result = privileged_read_db_->execSqlSync(kSql);
        std::vector<domain::Currency> currencies;
        currencies.reserve(result.size());
        for (const auto& row : result) {
            auto currency = domain::Currency::create(pg::getString(row, 0));
            if (!currency.has_value()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Stored account has an unsupported currency"));
            }
            currencies.push_back(*currency);
        }
        return currencies;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("list active currencies", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("list active currencies", error));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
