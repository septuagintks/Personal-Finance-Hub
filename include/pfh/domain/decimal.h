// Personal Finance Hub - Decimal Fixed-Point Type
// Version: 1.0
// C++23
//
// Decimal is a fixed-point decimal number with a fixed scale of 10 digits,
// backed by a 128-bit integer. It never uses binary floating point.
// Default rounding is Half-Even (banker's rounding).
//
// Decimal knows NOTHING about currency (see Money for currency binding).

#pragma once

#include "pfh/domain/domain_error.h"
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace pfh::domain {

/// @brief Fixed-point decimal number backed by a 128-bit integer.
///
/// Storage model: the real value is stored as an integer scaled by 10^kScale.
/// Example (kScale = 10): 12.34 is stored as 123400000000.
class Decimal {
public:
    // GCC/Clang native 128-bit integer. __extension__ suppresses the
    // -pedantic "ISO C++ does not support __int128" warning at the alias site.
    __extension__ using StorageType = __int128;
    __extension__ using UStorageType = unsigned __int128;

    /// @brief Number of fractional digits (fixed). Supports amounts (8) and
    /// exchange rates (10) without precision loss.
    static constexpr std::int32_t kScale = 10;

    /// @brief Scale factor = 10^kScale.
    static constexpr StorageType kScaleFactor = static_cast<StorageType>(10000000000LL);

    /// @brief Default constructor creates zero.
    constexpr Decimal() noexcept : value_(0) {}

    /// @brief Parse a decimal from a string (e.g. "-12.34", "0.0001").
    /// Fractional digits beyond kScale are rounded Half-Even.
    [[nodiscard]] static DomainResult<Decimal> parse(std::string_view text);

    /// @brief Build a Decimal from a whole integer value.
    [[nodiscard]] static DomainResult<Decimal> from_integer(std::int64_t value) noexcept;

    /// @brief Build a Decimal directly from a raw scaled 128-bit value.
    [[nodiscard]] static constexpr Decimal from_scaled(StorageType scaled) noexcept {
        return Decimal(scaled);
    }

    /// @brief Convert to canonical string (trailing zeros trimmed).
    [[nodiscard]] std::string to_string() const;

    // Arithmetic - all return DomainResult to surface overflow / divide-by-zero.
    [[nodiscard]] DomainResult<Decimal> add(const Decimal& other) const noexcept;
    [[nodiscard]] DomainResult<Decimal> subtract(const Decimal& other) const noexcept;
    [[nodiscard]] DomainResult<Decimal> multiply(const Decimal& other) const noexcept;
    [[nodiscard]] DomainResult<Decimal> divide(const Decimal& other) const noexcept;

    // Unary operations (cannot overflow within valid range).
    [[nodiscard]] Decimal negated() const noexcept { return Decimal(-value_); }
    [[nodiscard]] Decimal abs() const noexcept { return value_ < 0 ? Decimal(-value_) : *this; }

    // Predicates.
    [[nodiscard]] constexpr bool is_zero() const noexcept { return value_ == 0; }
    [[nodiscard]] constexpr bool is_negative() const noexcept { return value_ < 0; }
    [[nodiscard]] constexpr bool is_positive() const noexcept { return value_ > 0; }

    // Comparison (same scale, so raw comparison is exact).
    [[nodiscard]] constexpr auto operator<=>(const Decimal& other) const noexcept {
        return value_ <=> other.value_;
    }
    [[nodiscard]] constexpr bool operator==(const Decimal& other) const noexcept {
        return value_ == other.value_;
    }

    /// @brief Raw scaled 128-bit value (for persistence / testing).
    [[nodiscard]] constexpr StorageType raw_value() const noexcept { return value_; }

private:
    explicit constexpr Decimal(StorageType scaled) noexcept : value_(scaled) {}

    StorageType value_; // real value * 10^kScale
};

} // namespace pfh::domain
