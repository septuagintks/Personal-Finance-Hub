// Personal Finance Hub - In-Memory User / UserPreference Repositories
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_user_preference_repository.h"
#include "pfh/domain/repositories/i_user_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include <utility>

namespace pfh::infrastructure {

class InMemoryUserRepository final : public domain::IUserRepository {
public:
    explicit InMemoryUserRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_id(
        domain::UserId id) override {
        // Read-your-writes: staged (uncommitted) rows shadow committed ones, so
        // a user updated earlier in this transaction reads back its new value.
        if (store_.in_transaction) {
            if (auto it = store_.staged_users.find(id.value());
                it != store_.staged_users.end()) {
                return it->second.user;
            }
        }
        if (auto it = store_.users.find(id.value()); it != store_.users.end()) {
            return it->second.user;
        }
        return std::unexpected(domain::RepositoryError::not_found(
            "User not found: " + id.to_string()));
    }

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_username(
        const std::string& username) override {
        // Staged rows take precedence over committed for read-your-writes.
        if (store_.in_transaction) {
            for (const auto& [_, rec] : store_.staged_users) {
                if (rec.user.username() == username) {
                    return rec.user;
                }
            }
        }
        for (const auto& [id, rec] : store_.users) {
            // Skip a committed row that has been re-staged (updated) this
            // transaction; the staged loop above already returned the fresh copy.
            if (store_.in_transaction && store_.staged_users.contains(id)) {
                continue;
            }
            if (rec.user.username() == username) {
                return rec.user;
            }
        }
        return std::unexpected(domain::RepositoryError::not_found(
            "User not found by username: " + username));
    }

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_id(
        domain::ITransactionContext& /*tx*/,
        domain::UserId id) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "find_by_id(tx) requires an active transaction"));
        }
        return find_by_id(id);
    }

    [[nodiscard]] domain::RepositoryResult<domain::User> find_by_username(
        domain::ITransactionContext& /*tx*/,
        const std::string& username) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "find_by_username(tx) requires an active transaction"));
        }
        return find_by_username(username);
    }

    [[nodiscard]] domain::RepositoryResult<domain::UserId> create(
        domain::ITransactionContext& /*tx*/,
        const std::string& username,
        const std::string& password_hash,
        const domain::Currency& base_currency) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "create requires an active transaction"));
        }

        // Uniqueness check against committed + staged users.
        auto existing = find_by_username(username);
        if (existing.has_value()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Username already exists: " + username));
        }

        const auto id_value = store_.next_user_id++;
        domain::UserId id(id_value);
        domain::User user(id, username);
        InMemoryUserRecord rec{
            std::move(user),
            password_hash,
            base_currency,
            false};
        store_.staged_users.emplace(id_value, std::move(rec));
        return id;
    }

    [[nodiscard]] domain::RepositoryVoidResult save(
        domain::ITransactionContext& /*tx*/,
        const domain::User& user) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "save requires an active transaction"));
        }
        const auto id = user.id().value();
        if (store_.users.contains(id)) {
            auto rec = store_.users.at(id);
            rec.user = user;
            store_.staged_users.insert_or_assign(id, std::move(rec));
            return {};
        }
        if (store_.staged_users.contains(id)) {
            store_.staged_users.at(id).user = user;
            return {};
        }
        return std::unexpected(domain::RepositoryError::not_found(
            "Cannot save unknown user: " + user.id().to_string()));
    }

private:
    InMemoryStore& store_;
};

class InMemoryUserPreferenceRepository final : public domain::IUserPreferenceRepository {
public:
    explicit InMemoryUserPreferenceRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<domain::UserPreference> find_by_user(
        domain::UserId user_id) override {
        // Read-your-writes: a preference saved earlier in this transaction wins
        // over the committed row.
        if (store_.in_transaction) {
            if (auto it = store_.staged_preferences.find(user_id.value());
                it != store_.staged_preferences.end()) {
                return it->second;
            }
        }
        if (auto it = store_.preferences.find(user_id.value());
            it != store_.preferences.end()) {
            return it->second;
        }

        // Fallback: compose defaults from users.base_currency_code. Staged user
        // rows shadow committed ones here too.
        if (store_.in_transaction) {
            if (auto uit = store_.staged_users.find(user_id.value());
                uit != store_.staged_users.end()) {
                return domain::UserPreference(user_id, uit->second.base_currency);
            }
        }
        if (auto uit = store_.users.find(user_id.value()); uit != store_.users.end()) {
            return domain::UserPreference(user_id, uit->second.base_currency);
        }
        return std::unexpected(domain::RepositoryError::not_found(
            "User preference not found for user: " + user_id.to_string()));
    }

    [[nodiscard]] domain::RepositoryResult<domain::UserPreference> find_by_user(
        domain::ITransactionContext& /*tx*/,
        domain::UserId user_id) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "find_by_user(tx) requires an active transaction"));
        }
        return find_by_user(user_id);
    }

    [[nodiscard]] domain::RepositoryVoidResult save(
        domain::ITransactionContext& /*tx*/,
        const domain::UserPreference& preference) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "save requires an active transaction"));
        }
        const auto uid = preference.user_id().value();
        const bool user_exists = store_.staged_users.contains(uid) ||
                                 store_.users.contains(uid);
        if (!user_exists) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Cannot save preference for unknown user"));
        }
        store_.staged_preferences.insert_or_assign(
            uid, preference);

        // Keep users.base_currency_code in sync for fallback reads.
        if (store_.users.contains(uid)) {
            auto rec = store_.users.at(uid);
            rec.base_currency = preference.base_currency();
            store_.staged_users.insert_or_assign(uid, std::move(rec));
        } else if (store_.staged_users.contains(uid)) {
            store_.staged_users.at(uid).base_currency = preference.base_currency();
        }
        return {};
    }

private:
    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
