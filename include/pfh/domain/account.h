// Personal Finance Hub - Account Entity
// Version: 1.0
// C++23
//
// Account is an asset or liability container. All accounts support negative
// balances (for credit accounts, overdrafts, or adjustments). Balance is NOT
// stored in the Account entity itself; it is computed from Transaction history
// by BalanceCalculationService or cached in account_balance_cache.

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/typed_id.h"
#include "pfh/domain/user.h"
#include <chrono>
#include <optional>
#include <string>

namespace pfh::domain {

/// @brief Strongly-typed account identifier.
using AccountId = TypedId<AccountIdTag>;

/// @brief Account type classification.
enum class AccountType {
    Cash,
    Savings,
    Credit,
    DigitalWallet,
    Investment,
    Crypto,
    Other
};

/// @brief Account category for net worth calculation.
enum class AccountCategory {
    Asset,
    Liability
};

/// @brief Account entity — asset or liability container.
///
/// - Account name is mandatory and should be user-recognizable (e.g., "Chase
///   Checking", "Alipay Balance", not just "Bank Card").
/// - Balance is computed from Transactions, not stored here.
/// - Negative balances are allowed (credit accounts, overdrafts, adjustments).
/// - Archived accounts are excluded from active views but retained for history.
class Account {
public:
    using TimePoint = std::chrono::system_clock::time_point;

    Account(
        AccountId id,
        UserId owner,
        std::string name,
        AccountType type,
        std::string subtype,
        Currency currency,
        std::string description = "",
        bool is_archived = false,
        std::optional<TimePoint> archived_at = std::nullopt,
        TimePoint created_at = std::chrono::system_clock::now(),
        TimePoint updated_at = std::chrono::system_clock::now(),
        std::int64_t version = 1,
        std::optional<AccountCategory> category_override = std::nullopt)
        : id_(id),
          owner_(owner),
          name_(std::move(name)),
          type_(type),
          subtype_(std::move(subtype)),
          currency_(std::move(currency)),
          description_(std::move(description)),
          is_archived_(is_archived),
          archived_at_(archived_at),
          created_at_(created_at),
          updated_at_(updated_at),
          version_(version),
          category_override_(category_override) {}

    [[nodiscard]] AccountId id() const noexcept { return id_; }
    [[nodiscard]] UserId owner() const noexcept { return owner_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] AccountType type() const noexcept { return type_; }
    [[nodiscard]] const std::string& subtype() const noexcept { return subtype_; }
    [[nodiscard]] const Currency& currency() const noexcept { return currency_; }
    [[nodiscard]] const std::string& description() const noexcept { return description_; }
    [[nodiscard]] bool is_archived() const noexcept { return is_archived_; }
    [[nodiscard]] const std::optional<TimePoint>& archived_at() const noexcept { return archived_at_; }
    [[nodiscard]] TimePoint created_at() const noexcept { return created_at_; }
    [[nodiscard]] TimePoint updated_at() const noexcept { return updated_at_; }
    [[nodiscard]] std::int64_t version() const noexcept { return version_; }

    /// @brief The account's asset/liability category.
    ///
    /// Returns the user-set override when present; otherwise derives from type
    /// (Credit => Liability, everything else => Asset). The schema stores this
    /// as a dedicated `account_category` column, so a user can e.g. classify an
    /// Investment account as a Liability; the override is that persisted value.
    [[nodiscard]] AccountCategory category() const noexcept {
        if (category_override_.has_value()) {
            return *category_override_;
        }
        return default_category_for(type_);
    }

    /// @brief The category derived purely from the account type (ignores any
    /// override). Useful for computing the default to persist.
    [[nodiscard]] static AccountCategory default_category_for(AccountType type) noexcept {
        return (type == AccountType::Credit) ? AccountCategory::Liability
                                             : AccountCategory::Asset;
    }

    /// @brief True if the category was explicitly overridden by the user.
    [[nodiscard]] bool has_category_override() const noexcept {
        return category_override_.has_value();
    }

    /// @brief Archive the account (soft-hide from active views).
    ///
    /// Does NOT bump version_. The version is a persistence-owned optimistic-lock
    /// token: it reflects the stored row version the entity was loaded with, and
    /// the repository is the sole authority that validates it (loaded == stored)
    /// and increments it on a successful write. Bumping it here would make the
    /// entity's version diverge from the stored version before save() ever runs,
    /// so the optimistic-lock check could never pass.
    void archive(TimePoint archived_at) {
        is_archived_ = true;
        archived_at_ = archived_at;
        updated_at_ = archived_at;
    }

    /// @brief Unarchive the account. See archive() for why version_ is untouched.
    void unarchive(TimePoint unarchived_at) {
        is_archived_ = false;
        archived_at_ = std::nullopt;
        updated_at_ = unarchived_at;
    }

private:
    AccountId id_;
    UserId owner_;
    std::string name_;
    AccountType type_;
    std::string subtype_;
    Currency currency_;
    std::string description_;
    bool is_archived_;
    std::optional<TimePoint> archived_at_;
    TimePoint created_at_;
    TimePoint updated_at_;
    std::int64_t version_;
    // User override of the asset/liability classification; nullopt => derive
    // from type. Persisted as the account_category column.
    std::optional<AccountCategory> category_override_;
};

} // namespace pfh::domain
