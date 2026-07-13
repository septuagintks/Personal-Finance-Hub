// Personal Finance Hub - PostgreSQL UserPreference Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/user_preference_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

namespace pfh::infrastructure {

namespace {

domain::RepositoryError preference_not_found() {
    return domain::RepositoryError::not_found(
        "User preference owner not found");
}

domain::RepositoryResult<domain::UserPreference> load_preference(
    drogon::orm::Transaction& transaction,
    domain::UserId user_id) {
    // Keep fallback and preference lookup on the caller's pinned transaction.
    // The users row is global; the optional preference row is RLS scoped.
    constexpr const char* kSql = R"SQL(
        SELECT
            u.base_currency_code,
            p.user_id,
            p.base_currency_code,
            p.locale,
            p.timezone,
            p.date_format,
            p.number_format,
            p.theme::text,
            p.default_home_page::text,
            p.default_report_period::text
        FROM users u
        LEFT JOIN user_preferences p ON p.user_id = u.id
        WHERE u.id = $1
    )SQL";
    const auto result = transaction.execSqlSync(kSql, user_id.value());
    if (result.empty()) {
        return std::unexpected(preference_not_found());
    }

    const auto has_preference =
        pg::getOptionalBigInt(result[0], 1).has_value();
    const auto base_code = has_preference
        ? pg::getString(result[0], 2)
        : pg::getString(result[0], 0);
    auto base_currency = domain::Currency::create(base_code);
    if (!base_currency.has_value()) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored preference currency is unsupported"));
    }
    if (!has_preference) {
        return domain::UserPreference(user_id, *base_currency);
    }

    try {
        return domain::UserPreference(
            user_id,
            *base_currency,
            pg::getString(result[0], 3),
            pg::getString(result[0], 4),
            pg::getString(result[0], 5),
            pg::getString(result[0], 6),
            pg::parseThemeMode(pg::getString(result[0], 7)),
            pg::parseDefaultHomePage(pg::getString(result[0], 8)),
            pg::parseReportPeriod(pg::getString(result[0], 9)));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored user preference row is invalid"));
    }
}

}  // namespace

domain::RepositoryResult<domain::UserPreference>
UserPreferenceRepositoryImpl::find_by_user(domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(preference_not_found());
    }

    return postgres::execute_tenant_read<domain::UserPreference>(
        db_, tenant_user_id_, "find user preferences", [&](const auto& transaction) {
            return load_preference(*transaction, user_id);
        });
}

domain::RepositoryResult<domain::UserPreference>
UserPreferenceRepositoryImpl::find_by_user(
    domain::ITransactionContext& tx_iface,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(preference_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    try {
        return load_preference((*context)->transaction(), user_id);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "find user preferences in transaction", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "find user preferences in transaction", error));
    }
}

domain::RepositoryVoidResult UserPreferenceRepositoryImpl::save(
    domain::ITransactionContext& tx_iface,
    const domain::UserPreference& preference) {
    if (preference.user_id() != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::validation(
            "Preference owner does not match repository tenant"));
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }

    try {
        constexpr const char* kUpdateUserSql = R"SQL(
            UPDATE users SET base_currency_code = $1, updated_at = NOW()
            WHERE id = $2
            RETURNING id
        )SQL";
        const auto user_result = (*context)->transaction().execSqlSync(
            kUpdateUserSql,
            preference.base_currency().code(),
            preference.user_id().value());
        if (user_result.empty()) {
            return std::unexpected(preference_not_found());
        }

        constexpr const char* kUpsertSql = R"SQL(
            INSERT INTO user_preferences (
                user_id, base_currency_code, locale, timezone, date_format,
                number_format, theme, default_home_page, default_report_period)
            VALUES (
                $1, $2, $3, $4, $5, $6,
                $7::theme_mode, $8::default_home_page, $9::report_period)
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
        (*context)->transaction().execSqlSync(
            kUpsertSql,
            preference.user_id().value(),
            preference.base_currency().code(),
            preference.locale(),
            preference.timezone(),
            preference.date_format(),
            preference.number_format(),
            pg::toSqlText(preference.theme()),
            pg::toSqlText(preference.default_home_page()),
            pg::toSqlText(preference.default_report_period()));
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("save user preferences", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("save user preferences", error));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
