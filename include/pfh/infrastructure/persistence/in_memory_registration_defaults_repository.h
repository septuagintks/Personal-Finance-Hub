// Personal Finance Hub - In-Memory Registration Defaults Repository

#pragma once

#include "pfh/application/persistence/i_bootstrap_unit_of_work.h"
#include "pfh/application/ports/i_registration_defaults_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pfh::infrastructure {

class InMemoryRegistrationDefaultsRepository final
    : public application::IRegistrationDefaultsRepository {
public:
    explicit InMemoryRegistrationDefaultsRepository(InMemoryStore& store)
        : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<application::RegistrationDefaultsResult>
    initialize(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        const domain::Currency& base_currency,
        std::string_view preferred_locale,
        std::optional<std::string_view> preferred_timezone) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "Registration defaults require an active transaction"));
        }
        const auto* tenant_tx =
            dynamic_cast<const application::ITenantBootstrapTransaction*>(&tx);
        if (tenant_tx == nullptr || tenant_tx->tenant_user_id() != user_id) {
            return std::unexpected(domain::RepositoryError::validation(
                "Registration defaults tenant mismatch"));
        }

        auto user_it = store_.staged_users.find(user_id.value());
        if (user_it == store_.staged_users.end()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Registration user not found in transaction"));
        }
        if (user_it->second.categories_initialized) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Registration defaults already initialized"));
        }

        const auto locale = resolve_locale(preferred_locale);
        const auto timezone = preferred_timezone.has_value()
            ? std::string(*preferred_timezone)
            : locale == "en-US" ? "America/New_York" : "Asia/Shanghai";
        store_.staged_preferences.insert_or_assign(
            user_id.value(),
            domain::UserPreference(user_id, base_currency, locale, timezone));

        std::size_t inserted = 0;
        std::map<std::string_view, domain::CategoryId> roots;
        const auto& defaults = defaults_for(locale);
        for (std::size_t index = 0; index < defaults.size(); ++index) {
            const auto& item = defaults[index];
            std::optional<domain::CategoryId> parent_id;
            if (!item.parent.empty()) {
                const auto parent = roots.find(item.parent);
                if (parent == roots.end()) {
                    return std::unexpected(domain::RepositoryError::database(
                        "Registration category template parent is missing"));
                }
                parent_id = parent->second;
            }

            std::optional<domain::CategoryId> existing_id;
            const auto find_existing = [&](const auto& categories) {
                for (const auto& [id, category] : categories) {
                    if (category.owner() == user_id &&
                        category.name() == item.name &&
                        category.board() == item.board &&
                        category.parent_id() == parent_id &&
                        !category.is_deleted()) {
                        return std::optional<domain::CategoryId>(
                            domain::CategoryId(id));
                    }
                }
                return std::optional<domain::CategoryId>{};
            };
            existing_id = find_existing(store_.staged_categories);
            if (!existing_id.has_value()) {
                existing_id = find_existing(store_.categories);
            }
            if (existing_id.has_value()) {
                if (!parent_id.has_value()) {
                    roots.emplace(item.name, *existing_id);
                }
                continue;
            }

            const auto id = domain::CategoryId(store_.next_category_id++);
            store_.staged_categories.emplace(
                id.value(),
                domain::Category(
                    id,
                    user_id,
                    std::string(item.name),
                    item.board,
                    parent_id,
                    domain::CategorySource::System,
                    static_cast<std::int64_t>(
                        (locale == "en-US" ? 10'000 : 20'000) + index + 1),
                    item.sort_order));
            if (!parent_id.has_value()) {
                roots.emplace(item.name, id);
            }
            ++inserted;
        }

        user_it->second.categories_initialized = true;
        return application::RegistrationDefaultsResult{locale, inserted};
    }

private:
    struct DefaultCategory {
        std::string_view name;
        domain::CategoryBoard board;
        std::string_view parent;
        int sort_order;
    };

    [[nodiscard]] static const std::vector<DefaultCategory>& defaults_for(
        std::string_view locale) {
        using enum domain::CategoryBoard;
        static const std::vector<DefaultCategory> english{
            {"Food", Expense, {}, 1},
            {"Daily Living", Expense, {}, 2},
            {"Finance", Expense, {}, 3},
            {"Home", Expense, {}, 4},
            {"Clothing", Expense, {}, 5},
            {"Beauty", Expense, {}, 6},
            {"Electronics", Expense, {}, 7},
            {"Office", Expense, {}, 8},
            {"Social", Expense, {}, 9},
            {"Healthcare", Expense, {}, 10},
            {"Transportation", Expense, {}, 11},
            {"Fitness", Expense, {}, 12},
            {"Entertainment", Expense, {}, 13},
            {"Education", Expense, {}, 14},
            {"Travel", Expense, {}, 15},
            {"Pets", Expense, {}, 16},
            {"Family", Expense, {}, 17},
            {"Automotive", Expense, {}, 18},
            {"Campus", Expense, {}, 19},
            {"Gifts", Expense, {}, 20},
            {"Other", Expense, {}, 99},
            {"Salary", Income, {}, 1},
            {"Bonus", Income, {}, 2},
            {"Investments", Income, {}, 3},
            {"Part-time Work", Income, {}, 4},
            {"Side Business", Income, {}, 5},
            {"Cash Gifts", Income, {}, 6},
            {"Breakfast", Expense, "Food", 1},
            {"Lunch", Expense, "Food", 2},
            {"Dinner", Expense, "Food", 3},
            {"Coffee", Expense, "Food", 4},
            {"Delivery", Expense, "Food", 5},
            {"Dining Out", Expense, "Food", 6},
            {"Water", Expense, "Daily Living", 1},
            {"Electricity", Expense, "Daily Living", 2},
            {"Gas", Expense, "Daily Living", 3},
            {"Property Management", Expense, "Daily Living", 4},
            {"Household Supplies", Expense, "Daily Living", 5},
            {"Metro", Expense, "Transportation", 1},
            {"Bus", Expense, "Transportation", 2},
            {"Taxi", Expense, "Transportation", 3},
            {"Fuel", Expense, "Transportation", 4},
            {"Parking", Expense, "Transportation", 5},
            {"Fees", Expense, "Finance", 1},
            {"Interest Expense", Expense, "Finance", 2},
            {"Foreign Exchange Loss", Expense, "Finance", 3},
            {"Base Salary", Income, "Salary", 1},
            {"Performance Bonus", Income, "Salary", 2},
            {"Allowance", Income, "Salary", 3},
            {"Dividends", Income, "Investments", 1},
            {"Fund Returns", Income, "Investments", 2},
            {"Interest", Income, "Investments", 3},
            {"Capital Gains", Income, "Investments", 4},
            {"Family and Friends", Income, "Cash Gifts", 1},
            {"Platform Rewards", Income, "Cash Gifts", 2}};
        static const std::vector<DefaultCategory> chinese{
            {"\u9910\u996e", Expense, {}, 1},
            {"\u65e5\u5e38", Expense, {}, 2},
            {"\u8d22\u52a1", Expense, {}, 3},
            {"\u5c45\u5bb6", Expense, {}, 4},
            {"\u670d\u9970", Expense, {}, 5},
            {"\u7f8e\u5986", Expense, {}, 6},
            {"\u6570\u7801", Expense, {}, 7},
            {"\u529e\u516c", Expense, {}, 8},
            {"\u793e\u4ea4", Expense, {}, 9},
            {"\u533b\u7597", Expense, {}, 10},
            {"\u4ea4\u901a", Expense, {}, 11},
            {"\u8fd0\u52a8", Expense, {}, 12},
            {"\u5a31\u4e50", Expense, {}, 13},
            {"\u6559\u80b2", Expense, {}, 14},
            {"\u65c5\u884c", Expense, {}, 15},
            {"\u5ba0\u7269", Expense, {}, 16},
            {"\u5bb6\u5ead", Expense, {}, 17},
            {"\u6c7d\u8f66", Expense, {}, 18},
            {"\u6821\u56ed", Expense, {}, 19},
            {"\u4eba\u60c5", Expense, {}, 20},
            {"\u5176\u4ed6", Expense, {}, 99},
            {"\u5de5\u8d44", Income, {}, 1},
            {"\u5956\u91d1", Income, {}, 2},
            {"\u6295\u8d44", Income, {}, 3},
            {"\u517c\u804c", Income, {}, 4},
            {"\u526f\u4e1a", Income, {}, 5},
            {"\u7ea2\u5305", Income, {}, 6},
            {"\u65e9\u9910", Expense, "\u9910\u996e", 1},
            {"\u5348\u9910", Expense, "\u9910\u996e", 2},
            {"\u665a\u9910", Expense, "\u9910\u996e", 3},
            {"\u5496\u5561", Expense, "\u9910\u996e", 4},
            {"\u5916\u5356", Expense, "\u9910\u996e", 5},
            {"\u805a\u9910", Expense, "\u9910\u996e", 6},
            {"\u6c34\u8d39", Expense, "\u65e5\u5e38", 1},
            {"\u7535\u8d39", Expense, "\u65e5\u5e38", 2},
            {"\u71c3\u6c14\u8d39", Expense, "\u65e5\u5e38", 3},
            {"\u7269\u4e1a\u8d39", Expense, "\u65e5\u5e38", 4},
            {"\u751f\u6d3b\u7528\u54c1", Expense, "\u65e5\u5e38", 5},
            {"\u5730\u94c1", Expense, "\u4ea4\u901a", 1},
            {"\u516c\u4ea4", Expense, "\u4ea4\u901a", 2},
            {"\u6253\u8f66", Expense, "\u4ea4\u901a", 3},
            {"\u52a0\u6cb9", Expense, "\u4ea4\u901a", 4},
            {"\u505c\u8f66", Expense, "\u4ea4\u901a", 5},
            {"\u624b\u7eed\u8d39", Expense, "\u8d22\u52a1", 1},
            {"\u5229\u606f\u652f\u51fa", Expense, "\u8d22\u52a1", 2},
            {"\u6c47\u5151\u635f\u8017", Expense, "\u8d22\u52a1", 3},
            {"\u57fa\u672c\u5de5\u8d44", Income, "\u5de5\u8d44", 1},
            {"\u7ee9\u6548", Income, "\u5de5\u8d44", 2},
            {"\u8865\u8d34", Income, "\u5de5\u8d44", 3},
            {"\u80a1\u606f", Income, "\u6295\u8d44", 1},
            {"\u57fa\u91d1\u6536\u76ca", Income, "\u6295\u8d44", 2},
            {"\u5229\u606f", Income, "\u6295\u8d44", 3},
            {"\u5356\u51fa\u6536\u76ca", Income, "\u6295\u8d44", 4},
            {"\u4eb2\u53cb\u7ea2\u5305", Income, "\u7ea2\u5305", 1},
            {"\u5e73\u53f0\u7ea2\u5305", Income, "\u7ea2\u5305", 2}};
        return locale == "en-US" ? english : chinese;
    }

    [[nodiscard]] static std::string resolve_locale(std::string_view requested) {
        if (requested == "en-US" || requested == "en") {
            return "en-US";
        }
        return "zh-CN";
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
