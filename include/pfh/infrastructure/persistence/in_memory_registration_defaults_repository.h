// Personal Finance Hub - In-Memory Registration Defaults Repository

#pragma once

#include "pfh/application/persistence/i_bootstrap_unit_of_work.h"
#include "pfh/application/ports/i_registration_defaults_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <array>
#include <string>

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
        std::string_view preferred_locale) override {
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
        const auto timezone = locale == "en-US" ? "America/New_York"
                                                  : "Asia/Shanghai";
        store_.staged_preferences.insert_or_assign(
            user_id.value(),
            domain::UserPreference(user_id, base_currency, locale, timezone));

        struct DefaultCategory {
            const char* name;
            domain::CategoryBoard board;
            int sort_order;
        };
        const std::array defaults{
            DefaultCategory{"Salary", domain::CategoryBoard::Income, 1},
            DefaultCategory{"Investment Income", domain::CategoryBoard::Income, 2},
            DefaultCategory{"Food", domain::CategoryBoard::Expense, 1},
            DefaultCategory{"Transport", domain::CategoryBoard::Expense, 2}};

        std::size_t inserted = 0;
        for (const auto& item : defaults) {
            bool exists = false;
            for (const auto& [_, category] : store_.categories) {
                if (category.owner() == user_id && category.name() == item.name &&
                    category.board() == item.board && category.is_root() &&
                    !category.is_deleted()) {
                    exists = true;
                    break;
                }
            }
            if (exists) {
                continue;
            }
            const auto id = domain::CategoryId(store_.next_category_id++);
            store_.staged_categories.emplace(
                id.value(),
                domain::Category(
                    id,
                    user_id,
                    item.name,
                    item.board,
                    std::nullopt,
                    domain::CategorySource::System,
                    std::nullopt,
                    item.sort_order));
            ++inserted;
        }

        user_it->second.categories_initialized = true;
        return application::RegistrationDefaultsResult{locale, inserted};
    }

private:
    [[nodiscard]] static std::string resolve_locale(std::string_view requested) {
        if (requested == "en-US" || requested == "en") {
            return "en-US";
        }
        return "zh-CN";
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
