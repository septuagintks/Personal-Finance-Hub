// Personal Finance Hub - Strong Typed ID Base
// Version: 1.0
// C++23
// This file provides type-safe ID wrapper to prevent ID type confusion

#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <string>

namespace pfh::domain {

/// @brief Base template for strong typed IDs
/// @tparam Tag Tag type to distinguish different ID types
/// @tparam ValueType Underlying storage type (default: int64_t)
template <typename Tag, typename ValueType = std::int64_t>
class TypedId {
public:
    using value_type = ValueType;

    /// @brief Default constructor creates an invalid ID (0)
    constexpr TypedId() noexcept : value_(0) {}

    /// @brief Explicit constructor from raw value
    explicit constexpr TypedId(ValueType value) noexcept : value_(value) {}

    /// @brief Get the raw value
    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }

    /// @brief Check if ID is valid. Persisted IDs come from DB auto-increment
    /// sequences and are always positive; 0 (default/unset) and negative values
    /// are treated as invalid.
    [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ > 0; }

    /// @brief Spaceship operator for three-way comparison
    [[nodiscard]] constexpr auto operator<=>(const TypedId&) const noexcept = default;

    /// @brief Equality comparison
    [[nodiscard]] constexpr bool operator==(const TypedId&) const noexcept = default;

    /// @brief Convert to string
    [[nodiscard]] std::string to_string() const {
        return std::to_string(value_);
    }

private:
    ValueType value_;
};

// Tag types for different ID domains
struct UserIdTag {};
struct AccountIdTag {};
struct TransactionIdTag {};
struct CategoryIdTag {};
struct TagIdTag {};
struct ExchangeRateIdTag {};
struct SyncJobIdTag {};
struct AuditLogIdTag {};

// Strong typed ID definitions
using UserId = TypedId<UserIdTag>;
using AccountId = TypedId<AccountIdTag>;
using TransactionId = TypedId<TransactionIdTag>;
using CategoryId = TypedId<CategoryIdTag>;
using TagId = TypedId<TagIdTag>;
using ExchangeRateId = TypedId<ExchangeRateIdTag>;
using SyncJobId = TypedId<SyncJobIdTag>;
using AuditLogId = TypedId<AuditLogIdTag>;

} // namespace pfh::domain

// Hash support for use in std::unordered_map, std::unordered_set, etc.
namespace std {

template <typename Tag, typename ValueType>
struct hash<pfh::domain::TypedId<Tag, ValueType>> {
    [[nodiscard]] size_t operator()(const pfh::domain::TypedId<Tag, ValueType>& id) const noexcept {
        return hash<ValueType>{}(id.value());
    }
};

} // namespace std
