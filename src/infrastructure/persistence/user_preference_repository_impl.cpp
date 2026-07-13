// Personal Finance Hub - PostgreSQL UserPreference Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/user_preference_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/repositories/repository_error.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

namespace pfh::infrastructure {

domain::RepositoryResult<domain::UserPreference>
UserPreferenceRepositoryImpl::find_by_user(domain::UserId user_id) {
    constexpr const char* kPrefSql = R"SQL(
        SELECT base_currency_code, locale, timezone, date_format, number_format,
               theme::text, default_home_page::text, default_report_period::text
        FROM user_preferences WHERE user_id = $1
    )SQL";

    try {
        auto result = db_->execSqlSync(kPrefSql, user_id.value);
        if (!result.empty()) {
            const auto& row = result[0];
            const auto base_code = pg::getString(row, 0);
            const auto base = domain::Currency::create(base_code);
            if (!base.has_value()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Unknown currency in user_preferences: " + base_code));
            }
            return domain::UserPreference(
                user_id,
                *base,
                pg::getString(row, 1),
                pg::getString(row, 2),
                pg::getString(row, 3),
                pg::getString(row, 4),
                pg::parseThemeMode(pg::getString(row, 5)),
                pg::parseDefaultHomePage(pg::getString(row, 6)),
                pg::parseReportPeriod(pg::getString(row, 7)));
        }
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_user preferences failed: ") + e.base().what()));
    }

    // Fallback: no preferences row → compose from users.base_currency_code.
    constexpr const char* kUserSql = R"SQL(
        SELECT base_currency_code FROM users WHERE id = $1
    )SQL";
    try {
        auto result = db_->execSqlSync(kUserSql, user_id.value);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "User not found for preference fallback: " + user_id.to_string()));
        }
        const auto base_code = pg::getString(result[0], 0);
        const auto base = domain::Currency::create(base_code);
        if (!base.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Unknown currency in users: " + base_code));
        }
        return domain::UserPreference(user_id, *base);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("fallback to users failed: ") + e.base().what()));
    }
}

domain::RepositoryVoidResult UserPreferenceRepositoryImpl::save(
    domain::ITransactionContext& tx_iface,
    const domain::UserPreference& preference) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    // UPSERT: insert when no row, update when present. Keep users.base_currency_code
    // in sync (single source of truth for the fallback path).
    constexpr const char* kUpsertPrefSql = R"SQL(
        INSERT INTO user_preferences (
            user_id, base_currency_code, locale, timezone, date_format,
            number_format, theme, default_home_page, default_report_period)
        VALUES ($1, $2, $3, $4, $5, $6, $7::theme_mode, $8::default_home_page, $9::report_period)
        ON CONFLICT (user_id) DO UPDATE SET
            base_currency_code = EXCLUDED.base_currency_code,
            locale = EXCLUDED.locale,
            timezone = EXCLUDED.timezone,
            date_format = EXCLUDED.date_format,
            number_format = EXCLUDED.number_format,
            theme = EXCLUDED.theme,
            default_home_page = EXCLUDED.default_home_page,
            default_report_period = EXCLUDED.default_report_period,
            updated_at = NOW()
    )SQL";

    try {
        tx.execSqlSync(
            kUpsertPrefSql,
            preference.user_id().value,
            preference.base_currency().code(),
            preference.locale(),
            preference.timezone(),
            preference.date_format(),
            preference.number_format(),
            pg::toSqlText(preference.theme()),
            pg::toSqlText(preference.default_home_page()),
            pg::toSqlText(preference.default_report_period()));

        // Keep users.base_currency_code in sync for the fallback path.
        constexpr const char* kSyncUserSql = R"SQL(
            UPDATE users SET base_currency_code = $1, updated_at = NOW()
            WHERE id = $2
        )SQL";
        tx.execSqlSync(
            kSyncUserSql,
            preference.base_currency().code(),
            preference.user_id().value);

        return {};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("save preferences failed: ") + e.base().what()));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL