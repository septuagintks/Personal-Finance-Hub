// Personal Finance Hub - In-Memory Authentication Session Repository

#pragma once

#include "pfh/application/persistence/i_bootstrap_unit_of_work.h"
#include "pfh/application/ports/i_auth_session_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <algorithm>
#include <string>

namespace pfh::infrastructure {

class InMemoryAuthSessionRepository final
    : public application::IAuthSessionRepository {
public:
    explicit InMemoryAuthSessionRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<application::RefreshTokenRecord>
    find_refresh_token(std::string_view token_hash) override {
        const std::string key(token_hash);
        if (store_.in_transaction) {
            if (const auto it = store_.staged_refresh_tokens.find(key);
                it != store_.staged_refresh_tokens.end()) {
                return it->second;
            }
        }
        if (const auto it = store_.refresh_tokens.find(key);
            it != store_.refresh_tokens.end()) {
            return it->second;
        }
        return std::unexpected(domain::RepositoryError::not_found(
            "Refresh token not found"));
    }

    [[nodiscard]] domain::RepositoryResult<application::RefreshTokenRecord>
    find_refresh_token_for_update(
        domain::ITransactionContext& tx,
        std::string_view token_hash) override {
        if (auto active = require_active_transaction(tx); !active) {
            return std::unexpected(active.error());
        }
        auto token = find_refresh_token(token_hash);
        if (!token) {
            return token;
        }
        if (auto tenant = require_tenant(tx, token->user_id); !tenant) {
            return std::unexpected(tenant.error());
        }
        return token;
    }

    [[nodiscard]] domain::RepositoryVoidResult save_refresh_token(
        domain::ITransactionContext& tx,
        const application::RefreshTokenRecord& token) override {
        if (auto active = require_active_transaction(tx); !active) {
            return active;
        }
        if (!token.user_id.is_valid() || token.token_hash.empty() ||
            token.session_id.empty() || token.expires_at <= token.created_at) {
            return std::unexpected(domain::RepositoryError::validation(
                "Invalid refresh token record"));
        }
        if (auto tenant = require_tenant(tx, token.user_id); !tenant) {
            return tenant;
        }
        if (store_.refresh_tokens.contains(token.token_hash) ||
            store_.staged_refresh_tokens.contains(token.token_hash)) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Refresh token hash already exists"));
        }
        auto persisted = token;
        if (persisted.id <= 0) {
            persisted.id = store_.next_refresh_token_id++;
        }
        store_.staged_refresh_tokens.emplace(
            persisted.token_hash, std::move(persisted));
        return {};
    }

    [[nodiscard]] domain::RepositoryVoidResult revoke_refresh_token(
        domain::ITransactionContext& tx,
        std::string_view token_hash,
        application::AuthTimePoint revoked_at) override {
        auto token = find_refresh_token_for_update(tx, token_hash);
        if (!token) {
            return std::unexpected(token.error());
        }
        token->revoked_at = revoked_at;
        store_.staged_refresh_tokens.insert_or_assign(
            token->token_hash, *token);
        return {};
    }

    [[nodiscard]] domain::RepositoryVoidResult revoke_session(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        std::string_view session_id,
        application::AuthTimePoint revoked_at,
        application::AuthTimePoint expires_at,
        std::string_view reason) override {
        if (auto active = require_active_transaction(tx); !active) {
            return active;
        }
        if (auto tenant = require_tenant(tx, user_id); !tenant) {
            return tenant;
        }
        if (session_id.empty() || expires_at <= revoked_at) {
            return std::unexpected(domain::RepositoryError::validation(
                "Invalid revoked session record"));
        }

        auto stage_matching = [&](const auto& tokens) {
            for (const auto& [hash, token] : tokens) {
                if (token.user_id == user_id && token.session_id == session_id) {
                    auto revoked = token;
                    revoked.revoked_at = revoked_at;
                    store_.staged_refresh_tokens.insert_or_assign(hash, std::move(revoked));
                }
            }
        };
        stage_matching(store_.refresh_tokens);
        stage_matching(store_.staged_refresh_tokens);

        InMemoryRevokedSession session{
            user_id,
            std::string(session_id),
            expires_at,
            revoked_at,
            std::string(reason)};
        store_.staged_revoked_sessions.insert_or_assign(
            session.session_id, std::move(session));
        return {};
    }

    [[nodiscard]] domain::RepositoryVoidResult revoke_access_token(
        domain::ITransactionContext& tx,
        const application::AccessTokenClaims& claims,
        application::AuthTimePoint revoked_at) override {
        if (auto active = require_active_transaction(tx); !active) {
            return active;
        }
        if (auto tenant = require_tenant(tx, claims.user_id); !tenant) {
            return tenant;
        }
        if (claims.issuer.empty() || claims.token_id.empty() ||
            claims.expires_at <= revoked_at) {
            return std::unexpected(domain::RepositoryError::validation(
                "Invalid access token revocation"));
        }
        const auto key = claims.issuer + "\n" + claims.token_id;
        InMemoryRevokedAccessToken record{
            claims.issuer,
            claims.token_id,
            claims.session_id,
            claims.expires_at,
            revoked_at};
        store_.staged_revoked_access_tokens.insert_or_assign(key, std::move(record));
        return {};
    }

    [[nodiscard]] domain::RepositoryResult<bool> is_access_token_revoked(
        std::string_view issuer,
        std::string_view token_id,
        application::AuthTimePoint now) override {
        const auto key = std::string(issuer) + "\n" + std::string(token_id);
        auto lookup = [&](const auto& rows) {
            const auto it = rows.find(key);
            return it != rows.end() && it->second.expires_at > now;
        };
        return lookup(store_.staged_revoked_access_tokens) ||
               lookup(store_.revoked_access_tokens);
    }

    [[nodiscard]] domain::RepositoryResult<bool> is_session_revoked(
        std::string_view session_id,
        application::AuthTimePoint now) override {
        const std::string key(session_id);
        auto lookup = [&](const auto& rows) {
            const auto it = rows.find(key);
            return it != rows.end() && it->second.expires_at > now;
        };
        return lookup(store_.staged_revoked_sessions) ||
               lookup(store_.revoked_sessions);
    }

    [[nodiscard]] domain::RepositoryResult<bool> is_session_revoked(
        domain::ITransactionContext& tx,
        std::string_view session_id,
        application::AuthTimePoint now) override {
        if (auto active = require_active_transaction(tx); !active) {
            return std::unexpected(active.error());
        }
        return is_session_revoked(session_id, now);
    }

private:
    [[nodiscard]] domain::RepositoryVoidResult require_active_transaction(
        domain::ITransactionContext&) const {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "Authentication write requires an active transaction"));
        }
        return {};
    }

    [[nodiscard]] static domain::RepositoryVoidResult require_tenant(
        domain::ITransactionContext& tx,
        domain::UserId expected) {
        const auto* tenant_tx =
            dynamic_cast<const application::ITenantBootstrapTransaction*>(&tx);
        if (tenant_tx == nullptr || tenant_tx->tenant_user_id() != expected) {
            return std::unexpected(domain::RepositoryError::validation(
                "Authentication transaction tenant mismatch"));
        }
        return {};
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
