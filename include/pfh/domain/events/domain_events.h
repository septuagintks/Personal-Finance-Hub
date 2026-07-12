// Personal Finance Hub - Strongly-typed Domain Events
// Version: 1.0
// C++23
//
// Concrete IDomainEvent types for the Phase 1 event contract (14_Event_Design
// §2.3). Each event carries its required fields as typed members and renders a
// stable JSON payload that always includes userId and occurredAt, so S11
// consumers (OutboxPublisherJob, AuditLogHandler, cache invalidators) do not
// depend on ad-hoc per-use-case JSON.

#pragma once

#include "pfh/domain/account.h"
#include "pfh/domain/events/i_domain_event.h"
#include "pfh/domain/transaction.h"
#include "pfh/domain/typed_id.h"
#include "pfh/domain/user.h"
#include <chrono>
#include <cstdint>
#include <string>

namespace pfh::domain {

namespace event_detail {

// Seconds since epoch (UTC). Kept as an integer so payloads are stable and
// locale/format independent; consumers parse it back to a time_point.
[[nodiscard]] inline std::int64_t epoch_seconds(
    std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch())
        .count();
}

} // namespace event_detail

// Base for events that all carry userId + occurredAt (the common contract
// fields). Subclasses append their own typed fields to the JSON body.
class UserScopedEvent : public IDomainEvent {
public:
    UserScopedEvent(UserId user_id, std::chrono::system_clock::time_point occurred_at)
        : user_id_(user_id), occurred_at_(occurred_at) {}

    [[nodiscard]] std::chrono::system_clock::time_point occurred_at() const override {
        return occurred_at_;
    }

protected:
    // Render "userId" and "occurredAt" (epoch seconds); subclasses concatenate
    // their extra fields via extra_fields().
    [[nodiscard]] std::string base_payload() const {
        return "\"userId\":" + std::to_string(user_id_.value()) +
               ",\"occurredAt\":" +
               std::to_string(event_detail::epoch_seconds(occurred_at_));
    }

    UserId user_id_;
    std::chrono::system_clock::time_point occurred_at_;
};

/// @brief TransactionCreated: userId, transactionId, accountId, occurredAt.
class TransactionCreatedEvent final : public UserScopedEvent {
public:
    TransactionCreatedEvent(
        UserId user_id, TransactionId transaction_id, AccountId account_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          transaction_id_(transaction_id),
          account_id_(account_id) {}

    [[nodiscard]] std::string event_name() const override { return "TransactionCreated"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Transaction"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return transaction_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"transactionId\":" + std::to_string(transaction_id_.value()) +
               ",\"accountId\":" + std::to_string(account_id_.value()) + "}";
    }

private:
    TransactionId transaction_id_;
    AccountId account_id_;
};

/// @brief TransactionDeleted: userId, transactionId, accountId, occurredAt.
class TransactionDeletedEvent final : public UserScopedEvent {
public:
    TransactionDeletedEvent(
        UserId user_id, TransactionId transaction_id, AccountId account_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          transaction_id_(transaction_id),
          account_id_(account_id) {}

    [[nodiscard]] std::string event_name() const override { return "TransactionDeleted"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Transaction"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return transaction_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"transactionId\":" + std::to_string(transaction_id_.value()) +
               ",\"accountId\":" + std::to_string(account_id_.value()) + "}";
    }

private:
    TransactionId transaction_id_;
    AccountId account_id_;
};

/// @brief TransferCompleted: userId, transferGroupId, sourceAccountId,
/// targetAccountId, occurredAt.
class TransferCompletedEvent final : public UserScopedEvent {
public:
    TransferCompletedEvent(
        UserId user_id, TransferGroupId group_id,
        AccountId source_account_id, AccountId target_account_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          group_id_(group_id),
          source_account_id_(source_account_id),
          target_account_id_(target_account_id) {}

    [[nodiscard]] std::string event_name() const override { return "TransferCompleted"; }
    [[nodiscard]] std::string aggregate_type() const override { return "TransferGroup"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return group_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"transferGroupId\":" + std::to_string(group_id_.value()) +
               ",\"sourceAccountId\":" + std::to_string(source_account_id_.value()) +
               ",\"targetAccountId\":" + std::to_string(target_account_id_.value()) + "}";
    }

private:
    TransferGroupId group_id_;
    AccountId source_account_id_;
    AccountId target_account_id_;
};

/// @brief AccountDangerouslyDeleted: userId, accountId, occurredAt.
class AccountDangerouslyDeletedEvent final : public UserScopedEvent {
public:
    AccountDangerouslyDeletedEvent(
        UserId user_id, AccountId account_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at), account_id_(account_id) {}

    [[nodiscard]] std::string event_name() const override {
        return "AccountDangerouslyDeleted";
    }
    [[nodiscard]] std::string aggregate_type() const override { return "Account"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return account_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"accountId\":" + std::to_string(account_id_.value()) + "}";
    }

private:
    AccountId account_id_;
};

/// @brief ExchangeRateRefreshed: provider, baseCurrency, targetCurrency,
/// fetchedAt. Not user-scoped (system event), so it renders its own payload.
class ExchangeRateRefreshedEvent final : public IDomainEvent {
public:
    ExchangeRateRefreshedEvent(
        std::string provider, std::string base_currency,
        std::size_t refreshed_count,
        std::chrono::system_clock::time_point fetched_at)
        : provider_(std::move(provider)),
          base_currency_(std::move(base_currency)),
          refreshed_count_(refreshed_count),
          fetched_at_(fetched_at) {}

    [[nodiscard]] std::string event_name() const override { return "ExchangeRateRefreshed"; }
    [[nodiscard]] std::chrono::system_clock::time_point occurred_at() const override {
        return fetched_at_;
    }
    [[nodiscard]] std::string aggregate_type() const override { return "ExchangeRate"; }
    [[nodiscard]] std::string aggregate_id() const override { return base_currency_; }
    [[nodiscard]] std::string payload_json() const override {
        return "{\"provider\":\"" + provider_ + "\"" +
               ",\"baseCurrency\":\"" + base_currency_ + "\"" +
               ",\"refreshedCount\":" + std::to_string(refreshed_count_) +
               ",\"fetchedAt\":" +
               std::to_string(event_detail::epoch_seconds(fetched_at_)) + "}";
    }

private:
    std::string provider_;
    std::string base_currency_;
    std::size_t refreshed_count_;
    std::chrono::system_clock::time_point fetched_at_;
};

/// @brief ExchangeRateRefreshFailed: emitted when the provider is unavailable
/// and the refresh degrades to existing historical rates. Carries whether any
/// usable historical rate exists so an alert handler can escalate a hard outage
/// (degraded AND no fallback) differently from a soft one.
class ExchangeRateRefreshFailedEvent final : public IDomainEvent {
public:
    ExchangeRateRefreshFailedEvent(
        std::string provider, std::string base_currency,
        bool historical_available, std::string reason,
        std::chrono::system_clock::time_point occurred_at)
        : provider_(std::move(provider)),
          base_currency_(std::move(base_currency)),
          historical_available_(historical_available),
          reason_(std::move(reason)),
          occurred_at_(occurred_at) {}

    [[nodiscard]] std::string event_name() const override {
        return "ExchangeRateRefreshFailed";
    }
    [[nodiscard]] std::chrono::system_clock::time_point occurred_at() const override {
        return occurred_at_;
    }
    [[nodiscard]] std::string aggregate_type() const override { return "ExchangeRate"; }
    [[nodiscard]] std::string aggregate_id() const override { return base_currency_; }
    [[nodiscard]] std::string payload_json() const override {
        return "{\"provider\":\"" + provider_ + "\"" +
               ",\"baseCurrency\":\"" + base_currency_ + "\"" +
               ",\"historicalAvailable\":" +
               (historical_available_ ? "true" : "false") +
               ",\"reason\":\"" + reason_ + "\"" +
               ",\"occurredAt\":" +
               std::to_string(event_detail::epoch_seconds(occurred_at_)) + "}";
    }

private:
    std::string provider_;
    std::string base_currency_;
    bool historical_available_;
    std::string reason_;
    std::chrono::system_clock::time_point occurred_at_;
};

} // namespace pfh::domain
