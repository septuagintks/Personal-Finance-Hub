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
#include "pfh/domain/category.h"
#include "pfh/domain/events/i_domain_event.h"
#include "pfh/domain/transaction.h"
#include "pfh/domain/typed_id.h"
#include "pfh/domain/user.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace pfh::domain {

namespace event_detail {

// Seconds since epoch (UTC). Kept as an integer so payloads are stable and
// locale/format independent; consumers parse it back to a time_point.
[[nodiscard]] inline std::int64_t epoch_seconds(
    std::chrono::system_clock::time_point tp) {
    return std::chrono::floor<std::chrono::seconds>(tp.time_since_epoch()).count();
}

[[nodiscard]] inline std::string json_string(std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                result += "\\u00";
                result.push_back(kHex[(c >> 4) & 0x0f]);
                result.push_back(kHex[c & 0x0f]);
            } else {
                result.push_back(static_cast<char>(c));
            }
        }
    }
    result.push_back('"');
    return result;
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

class AccountArchivedEvent final : public UserScopedEvent {
public:
    AccountArchivedEvent(
        UserId user_id, AccountId account_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at), account_id_(account_id) {}

    [[nodiscard]] std::string event_name() const override { return "AccountArchived"; }
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

class AccountUpdatedEvent final : public UserScopedEvent {
public:
    AccountUpdatedEvent(
        UserId user_id, AccountId account_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at), account_id_(account_id) {}

    [[nodiscard]] std::string event_name() const override { return "AccountUpdated"; }
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

class AccountRestoredEvent final : public UserScopedEvent {
public:
    AccountRestoredEvent(
        UserId user_id, AccountId account_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at), account_id_(account_id) {}

    [[nodiscard]] std::string event_name() const override { return "AccountRestored"; }
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

class CategoryCreatedEvent final : public UserScopedEvent {
public:
    CategoryCreatedEvent(
        UserId user_id, CategoryId category_id, CategoryBoard board,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          category_id_(category_id),
          board_(board) {}

    [[nodiscard]] std::string event_name() const override { return "CategoryCreated"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Category"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return category_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"categoryId\":" + std::to_string(category_id_.value()) +
               ",\"board\":" + event_detail::json_string(
                   board_ == CategoryBoard::Income ? "income" : "expense") + "}";
    }

private:
    CategoryId category_id_;
    CategoryBoard board_;
};

class CategoryDeletedEvent final : public UserScopedEvent {
public:
    CategoryDeletedEvent(
        UserId user_id, CategoryId category_id, CategoryBoard board,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          category_id_(category_id),
          board_(board) {}

    [[nodiscard]] std::string event_name() const override { return "CategoryDeleted"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Category"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return category_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"categoryId\":" + std::to_string(category_id_.value()) +
               ",\"board\":" + event_detail::json_string(
                   board_ == CategoryBoard::Income ? "income" : "expense") + "}";
    }

private:
    CategoryId category_id_;
    CategoryBoard board_;
};

class CategoryUpdatedEvent final : public UserScopedEvent {
public:
    CategoryUpdatedEvent(
        UserId user_id, CategoryId category_id, CategoryBoard board,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          category_id_(category_id), board_(board) {}

    [[nodiscard]] std::string event_name() const override { return "CategoryUpdated"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Category"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return category_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"categoryId\":" + std::to_string(category_id_.value()) +
               ",\"board\":" + event_detail::json_string(
                   board_ == CategoryBoard::Income ? "income" : "expense") + "}";
    }

private:
    CategoryId category_id_;
    CategoryBoard board_;
};

class CategoryRestoredEvent final : public UserScopedEvent {
public:
    CategoryRestoredEvent(
        UserId user_id, CategoryId category_id, CategoryBoard board,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          category_id_(category_id), board_(board) {}

    [[nodiscard]] std::string event_name() const override { return "CategoryRestored"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Category"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return category_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"categoryId\":" + std::to_string(category_id_.value()) +
               ",\"board\":" + event_detail::json_string(
                   board_ == CategoryBoard::Income ? "income" : "expense") + "}";
    }

private:
    CategoryId category_id_;
    CategoryBoard board_;
};

class TagCreatedEvent final : public UserScopedEvent {
public:
    TagCreatedEvent(
        UserId user_id, TagId tag_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at), tag_id_(tag_id) {}
    [[nodiscard]] std::string event_name() const override { return "TagCreated"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Tag"; }
    [[nodiscard]] std::string aggregate_id() const override { return tag_id_.to_string(); }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"tagId\":" + std::to_string(tag_id_.value()) + "}";
    }
private:
    TagId tag_id_;
};

class TagUpdatedEvent final : public UserScopedEvent {
public:
    TagUpdatedEvent(
        UserId user_id, TagId tag_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at), tag_id_(tag_id) {}
    [[nodiscard]] std::string event_name() const override { return "TagUpdated"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Tag"; }
    [[nodiscard]] std::string aggregate_id() const override { return tag_id_.to_string(); }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"tagId\":" + std::to_string(tag_id_.value()) + "}";
    }
private:
    TagId tag_id_;
};

class TagDeletedEvent final : public UserScopedEvent {
public:
    TagDeletedEvent(
        UserId user_id, TagId tag_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at), tag_id_(tag_id) {}
    [[nodiscard]] std::string event_name() const override { return "TagDeleted"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Tag"; }
    [[nodiscard]] std::string aggregate_id() const override { return tag_id_.to_string(); }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"tagId\":" + std::to_string(tag_id_.value()) + "}";
    }
private:
    TagId tag_id_;
};

class TagRestoredEvent final : public UserScopedEvent {
public:
    TagRestoredEvent(
        UserId user_id, TagId tag_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at), tag_id_(tag_id) {}
    [[nodiscard]] std::string event_name() const override { return "TagRestored"; }
    [[nodiscard]] std::string aggregate_type() const override { return "Tag"; }
    [[nodiscard]] std::string aggregate_id() const override { return tag_id_.to_string(); }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"tagId\":" + std::to_string(tag_id_.value()) + "}";
    }
private:
    TagId tag_id_;
};

class UserPreferenceUpdatedEvent final : public UserScopedEvent {
public:
    UserPreferenceUpdatedEvent(
        UserId user_id, std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at) {}

    [[nodiscard]] std::string event_name() const override {
        return "UserPreferenceUpdated";
    }
    [[nodiscard]] std::string aggregate_type() const override { return "UserPreference"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return user_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() + "}";
    }
};

class UserRegisteredEvent final : public UserScopedEvent {
public:
    UserRegisteredEvent(
        UserId user_id,
        std::string locale,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at), locale_(std::move(locale)) {}

    [[nodiscard]] std::string event_name() const override { return "UserRegistered"; }
    [[nodiscard]] std::string aggregate_type() const override { return "User"; }
    [[nodiscard]] std::string aggregate_id() const override {
        return user_id_.to_string();
    }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"locale\":" + event_detail::json_string(locale_) + "}";
    }

private:
    std::string locale_;
};

class UserLoggedInEvent final : public UserScopedEvent {
public:
    UserLoggedInEvent(
        UserId user_id,
        std::string session_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          session_id_(std::move(session_id)) {}

    [[nodiscard]] std::string event_name() const override { return "UserLoggedIn"; }
    [[nodiscard]] std::string aggregate_type() const override { return "AuthSession"; }
    [[nodiscard]] std::string aggregate_id() const override { return session_id_; }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"sessionId\":" + event_detail::json_string(session_id_) + "}";
    }

private:
    std::string session_id_;
};

class TokenRefreshedEvent final : public UserScopedEvent {
public:
    TokenRefreshedEvent(
        UserId user_id,
        std::string session_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          session_id_(std::move(session_id)) {}

    [[nodiscard]] std::string event_name() const override { return "TokenRefreshed"; }
    [[nodiscard]] std::string aggregate_type() const override { return "AuthSession"; }
    [[nodiscard]] std::string aggregate_id() const override { return session_id_; }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"sessionId\":" + event_detail::json_string(session_id_) + "}";
    }

private:
    std::string session_id_;
};

class UserLoggedOutEvent final : public UserScopedEvent {
public:
    UserLoggedOutEvent(
        UserId user_id,
        std::string session_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          session_id_(std::move(session_id)) {}

    [[nodiscard]] std::string event_name() const override { return "UserLoggedOut"; }
    [[nodiscard]] std::string aggregate_type() const override { return "AuthSession"; }
    [[nodiscard]] std::string aggregate_id() const override { return session_id_; }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"sessionId\":" + event_detail::json_string(session_id_) + "}";
    }

private:
    std::string session_id_;
};

class RefreshTokenReuseDetectedEvent final : public UserScopedEvent {
public:
    RefreshTokenReuseDetectedEvent(
        UserId user_id,
        std::string session_id,
        std::chrono::system_clock::time_point occurred_at)
        : UserScopedEvent(user_id, occurred_at),
          session_id_(std::move(session_id)) {}

    [[nodiscard]] std::string event_name() const override {
        return "RefreshTokenReuseDetected";
    }
    [[nodiscard]] std::string aggregate_type() const override { return "AuthSession"; }
    [[nodiscard]] std::string aggregate_id() const override { return session_id_; }
    [[nodiscard]] std::string payload_json() const override {
        return "{" + base_payload() +
               ",\"sessionId\":" + event_detail::json_string(session_id_) + "}";
    }

private:
    std::string session_id_;
};

/// @brief ExchangeRateRefreshed: provider, baseCurrency, targetCurrency,
/// fetchedAt. Not user-scoped (system event), so it renders its own payload.
class ExchangeRateRefreshedEvent final : public IDomainEvent {
public:
    ExchangeRateRefreshedEvent(
        std::string provider, std::string base_currency,
        std::string target_currency,
        std::chrono::system_clock::time_point fetched_at)
        : provider_(std::move(provider)),
          base_currency_(std::move(base_currency)),
          target_currency_(std::move(target_currency)),
          fetched_at_(fetched_at) {}

    [[nodiscard]] std::string event_name() const override { return "ExchangeRateRefreshed"; }
    [[nodiscard]] std::chrono::system_clock::time_point occurred_at() const override {
        return fetched_at_;
    }
    [[nodiscard]] std::string aggregate_type() const override { return "ExchangeRate"; }
    [[nodiscard]] std::string aggregate_id() const override { return base_currency_; }
    [[nodiscard]] std::string payload_json() const override {
        return "{\"provider\":" + event_detail::json_string(provider_) +
               ",\"baseCurrency\":" + event_detail::json_string(base_currency_) +
               ",\"targetCurrency\":" + event_detail::json_string(target_currency_) +
               ",\"fetchedAt\":" +
               std::to_string(event_detail::epoch_seconds(fetched_at_)) + "}";
    }

private:
    std::string provider_;
    std::string base_currency_;
    std::string target_currency_;
    std::chrono::system_clock::time_point fetched_at_;
};

/// @brief ExchangeRateRefreshFailed: emitted when the provider is unavailable
/// and the refresh degrades to existing historical rates. Carries whether all
/// requested historical rates exist so an alert handler can escalate a hard outage
/// (degraded AND no fallback) differently from a soft one.
class ExchangeRateRefreshFailedEvent final : public IDomainEvent {
public:
    ExchangeRateRefreshFailedEvent(
        std::string provider, std::string base_currency,
        bool historical_fallback_available, std::string reason,
        std::chrono::system_clock::time_point occurred_at)
        : provider_(std::move(provider)),
          base_currency_(std::move(base_currency)),
          historical_fallback_available_(historical_fallback_available),
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
        return "{\"provider\":" + event_detail::json_string(provider_) +
               ",\"baseCurrency\":" + event_detail::json_string(base_currency_) +
               ",\"historicalAvailable\":" +
               (historical_fallback_available_ ? "true" : "false") +
               ",\"reason\":" + event_detail::json_string(reason_) +
               ",\"occurredAt\":" +
               std::to_string(event_detail::epoch_seconds(occurred_at_)) + "}";
    }

private:
    std::string provider_;
    std::string base_currency_;
    bool historical_fallback_available_;
    std::string reason_;
    std::chrono::system_clock::time_point occurred_at_;
};

} // namespace pfh::domain
