// Personal Finance Hub - In-Memory Persistence Store
// Version: 1.0
// C++23
//
// Shared mutable store used by in-memory repositories and unit of work.
// Models the same consistency rules as the PostgreSQL schema so repository
// integration tests can run without a live database.
//
// Drogon/PostgreSQL implementations will replace these maps with SQL while
// keeping the domain repository interfaces unchanged.

#pragma once

#include "pfh/application/security/auth_models.h"
#include "pfh/domain/account.h"
#include "pfh/domain/audit_log.h"
#include "pfh/domain/category.h"
#include "pfh/domain/exchange_rate.h"
#include "pfh/domain/transaction.h"
#include "pfh/domain/user.h"
#include "pfh/domain/user_preference.h"
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

struct OutboxRecord {
    std::string id;
    std::string event_name;
    std::string aggregate_type;
    std::string aggregate_id;
    std::string payload_json;
    std::string status; // pending | published | failed | dead_letter
    int retry_count = 0;
    // When the event fact occurred. Consumers (audit, cache invalidation) rely
    // on this; the domain_events_outbox table has an occurred_at column.
    std::chrono::system_clock::time_point occurred_at{};
};

struct InMemoryUserRecord {
    domain::User user;
    std::string password_hash;
    domain::Currency base_currency;
    bool categories_initialized = false;

    InMemoryUserRecord(
        domain::User user_in,
        std::string password_hash_in,
        domain::Currency base_currency_in,
        bool categories_initialized_in = false)
        : user(std::move(user_in)),
          password_hash(std::move(password_hash_in)),
          base_currency(std::move(base_currency_in)),
          categories_initialized(categories_initialized_in) {}
};

struct InMemoryBalanceCache {
    domain::Money balance;
    domain::TransactionId last_transaction_id;
    std::int64_t source_version = 0;
    std::int64_t cache_version = 1;

    InMemoryBalanceCache(
        domain::Money balance_in,
        domain::TransactionId last_transaction_id_in,
        std::int64_t source_version_in,
        std::int64_t cache_version_in)
        : balance(std::move(balance_in)),
          last_transaction_id(last_transaction_id_in),
          source_version(source_version_in),
          cache_version(cache_version_in) {}
};

struct InMemoryTransferGroup {
    domain::TransferGroupId id;
    domain::UserId user_id;
    int transfer_mode = 0;
    std::optional<domain::ExchangeRate> rate;

    InMemoryTransferGroup(
        domain::TransferGroupId id_in,
        domain::UserId user_id_in,
        int transfer_mode_in,
        std::optional<domain::ExchangeRate> rate_in)
        : id(id_in),
          user_id(user_id_in),
          transfer_mode(transfer_mode_in),
          rate(std::move(rate_in)) {}
};

struct InMemoryRevokedAccessToken {
    std::string issuer;
    std::string token_id;
    std::string session_id;
    std::chrono::system_clock::time_point expires_at{};
    std::chrono::system_clock::time_point revoked_at{};
};

struct InMemoryRevokedSession {
    domain::UserId user_id;
    std::string session_id;
    std::chrono::system_clock::time_point expires_at{};
    std::chrono::system_clock::time_point revoked_at{};
    std::string reason;
};

/// @brief Process-local store. Not thread-safe; one store per test fixture.
struct InMemoryStore {
    std::int64_t next_user_id = 1;
    std::int64_t next_account_id = 1;
    std::int64_t next_transaction_id = 1;
    std::int64_t next_transfer_group_id = 1;
    std::int64_t next_exchange_rate_id = 1;
    std::uint64_t next_tx_context_id = 1;
    std::int64_t next_outbox_id = 1;
    std::int64_t next_refresh_token_id = 1;

    std::int64_t next_category_id = 1;

    std::map<std::int64_t, InMemoryUserRecord> users;
    std::map<std::int64_t, domain::UserPreference> preferences;
    std::map<std::int64_t, domain::Account> accounts;
    std::map<std::int64_t, domain::Transaction> transactions;
    std::map<std::int64_t, domain::Category> categories;
    std::map<std::int64_t, InMemoryTransferGroup> transfer_groups;
    std::map<std::int64_t, InMemoryBalanceCache> balance_cache;
    // Append-only: key is exchange_rate id, order of insertion preserved by map id.
    std::map<std::int64_t, domain::ExchangeRate> exchange_rates;
    std::vector<OutboxRecord> outbox;
    std::map<std::string, application::RefreshTokenRecord> refresh_tokens;
    std::map<std::string, InMemoryRevokedAccessToken> revoked_access_tokens;
    std::map<std::string, InMemoryRevokedSession> revoked_sessions;
    std::vector<domain::AuditLogEntry> audit_logs;

    // Pending staging for the active unit-of-work transaction.
    // On commit these become permanent; on rollback they are discarded.
    bool in_transaction = false;
    std::map<std::int64_t, InMemoryUserRecord> staged_users;
    std::map<std::int64_t, domain::UserPreference> staged_preferences;
    std::map<std::int64_t, domain::Account> staged_accounts;
    std::map<std::int64_t, domain::Transaction> staged_transactions;
    std::map<std::int64_t, domain::Category> staged_categories;
    std::map<std::int64_t, InMemoryTransferGroup> staged_transfer_groups;
    std::map<std::int64_t, InMemoryBalanceCache> staged_balance_cache;
    std::map<std::int64_t, domain::ExchangeRate> staged_exchange_rates;
    std::vector<OutboxRecord> staged_outbox;
    std::map<std::string, application::RefreshTokenRecord> staged_refresh_tokens;
    std::map<std::string, InMemoryRevokedAccessToken> staged_revoked_access_tokens;
    std::map<std::string, InMemoryRevokedSession> staged_revoked_sessions;
    std::vector<domain::AuditLogEntry> staged_audit_logs;
    std::vector<std::int64_t> staged_deleted_accounts;
    std::vector<std::int64_t> staged_deleted_transactions;
    std::vector<std::int64_t> staged_deleted_balance_cache;
    std::vector<std::int64_t> staged_deleted_transfer_groups;
    std::vector<std::int64_t> staged_deleted_categories;
};

} // namespace pfh::infrastructure
