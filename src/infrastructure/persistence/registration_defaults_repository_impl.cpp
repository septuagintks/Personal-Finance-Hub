// Personal Finance Hub - PostgreSQL Registration Defaults Repository

#include "pfh/infrastructure/persistence/registration_defaults_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace pfh::infrastructure {

namespace {

[[nodiscard]] std::string primary_language(std::string_view locale) {
    const auto separator = locale.find('-');
    return std::string(locale.substr(0, separator));
}

[[nodiscard]] std::string timezone_for(std::string_view locale) {
    if (locale == "en-US" || locale == "en" || locale.starts_with("en-")) {
        return "America/New_York";
    }
    if (locale == "ja-JP") {
        return "Asia/Tokyo";
    }
    return "Asia/Shanghai";
}

[[nodiscard]] domain::RepositoryResult<std::string> resolve_locale(
    drogon::orm::Transaction& transaction,
    std::string_view preferred_locale) {
    std::vector<std::string> candidates;
    auto append_unique = [&](std::string value) {
        if (!value.empty() &&
            std::find(candidates.begin(), candidates.end(), value) == candidates.end()) {
            candidates.push_back(std::move(value));
        }
    };
    append_unique(std::string(preferred_locale));
    append_unique(primary_language(preferred_locale));
    append_unique("en-US");
    append_unique("zh-CN");

    constexpr const char* kExistsSql = R"SQL(
        SELECT 1
        FROM system_category_templates
        WHERE locale = $1 AND default_board IS NOT NULL
        LIMIT 1
    )SQL";
    for (const auto& candidate : candidates) {
        if (!transaction.execSqlSync(kExistsSql, candidate).empty()) {
            return candidate;
        }
    }
    return std::unexpected(domain::RepositoryError::database(
        "No category templates are available for registration"));
}

} // namespace

domain::RepositoryResult<application::RegistrationDefaultsResult>
RegistrationDefaultsRepositoryImpl::initialize(
    domain::ITransactionContext& tx_iface,
    domain::UserId user_id,
    const domain::Currency& base_currency,
    std::string_view preferred_locale) {
    auto context = postgres::require_transaction(tx_iface, user_id);
    if (!context) {
        return std::unexpected(context.error());
    }
    try {
        auto locale = resolve_locale(
            (*context)->transaction(), preferred_locale);
        if (!locale) {
            return std::unexpected(locale.error());
        }

        constexpr const char* kPreferenceSql = R"SQL(
            INSERT INTO user_preferences (
                user_id, base_currency_code, locale, timezone)
            VALUES ($1, $2, $3, $4)
            ON CONFLICT (user_id) DO NOTHING
        )SQL";
        const auto preference = (*context)->transaction().execSqlSync(
            kPreferenceSql,
            user_id.value(),
            base_currency.code(),
            *locale,
            timezone_for(*locale));
        if (preference.affectedRows() != 1) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Registration defaults already initialized"));
        }

        constexpr const char* kRootSql = R"SQL(
            INSERT INTO categories (
                user_id, name, parent_id, board, source,
                template_id, sort_order)
            SELECT $1, template.name, NULL,
                   template.default_board, 'system'::category_source,
                   template.id, template.sort_order
            FROM system_category_templates template
            WHERE template.locale = $2
              AND template.default_board IS NOT NULL
              AND template.parent_id IS NULL
            ON CONFLICT (user_id, board, parent_id, name)
            DO UPDATE SET
                source = EXCLUDED.source,
                template_id = EXCLUDED.template_id,
                sort_order = EXCLUDED.sort_order,
                deleted_at = NULL,
                updated_at = NOW()
        )SQL";
        const auto roots = (*context)->transaction().execSqlSync(
            kRootSql, user_id.value(), *locale);

        constexpr const char* kChildSql = R"SQL(
            INSERT INTO categories (
                user_id, name, parent_id, board, source,
                template_id, sort_order)
            SELECT $1, child.name, parent_category.id,
                   child.default_board, 'system'::category_source,
                   child.id, child.sort_order
            FROM system_category_templates child
            JOIN categories parent_category
              ON parent_category.user_id = $1
             AND parent_category.template_id = child.parent_id
             AND parent_category.deleted_at IS NULL
            WHERE child.locale = $2
              AND child.default_board IS NOT NULL
              AND child.parent_id IS NOT NULL
            ON CONFLICT (user_id, board, parent_id, name)
            DO UPDATE SET
                source = EXCLUDED.source,
                template_id = EXCLUDED.template_id,
                sort_order = EXCLUDED.sort_order,
                deleted_at = NULL,
                updated_at = NOW()
        )SQL";
        const auto children = (*context)->transaction().execSqlSync(
            kChildSql, user_id.value(), *locale);
        const auto category_count =
            roots.affectedRows() + children.affectedRows();
        if (category_count == 0) {
            return std::unexpected(domain::RepositoryError::database(
                "Registration category templates produced no categories"));
        }

        constexpr const char* kMarkInitialized = R"SQL(
            UPDATE users
            SET categories_initialized = TRUE, updated_at = NOW()
            WHERE id = $1 AND categories_initialized = FALSE
        )SQL";
        const auto marked = (*context)->transaction().execSqlSync(
            kMarkInitialized, user_id.value());
        if (marked.affectedRows() != 1) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Registration defaults already initialized"));
        }

        return application::RegistrationDefaultsResult{
            *locale, category_count};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "initialize registration defaults", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "initialize registration defaults", error));
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
