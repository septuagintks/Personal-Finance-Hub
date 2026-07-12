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

    /// @brief Parse a user-supplied amount for a PostgreSQL NUMERIC(20,8)
    /// boundary. Non-zero precision beyond 8 fractional digits is rejected
    /// before Decimal's internal scale-10 parser can round it away.
    [[nodiscard]] static DomainResult<Decimal> parse_numeric_20_8(
        std::string_view text);

    /// @brief Parse a user-supplied exchange rate for NUMERIC(20,10).
    [[nodiscard]] static DomainResult<Decimal> parse_numeric_20_10(
        std::string_view text);

    /// @brief Build a Decimal from a whole integer value.
    [[nodiscard]] static DomainResult<Decimal> from_integer(std::int64_t value);

    /// @brief Build a Decimal directly from a raw scaled 128-bit value.
    [[nodiscard]] static constexpr Decimal from_scaled(StorageType scaled) noexcept {
        return Decimal(scaled);
    }

    /// @brief Convert to canonical string (trailing zeros trimmed).
    [[nodiscard]] std::string to_string() const;

    // Arithmetic - all return DomainResult to surface overflow / divide-by-zero.
    // Not noexcept: the error path constructs a DomainError holding a
    // std::string message, which may allocate.
    [[nodiscard]] DomainResult<Decimal> add(const Decimal& other) const;
    [[nodiscard]] DomainResult<Decimal> subtract(const Decimal& other) const;
    [[nodiscard]] DomainResult<Decimal> multiply(const Decimal& other) const;
    [[nodiscard]] DomainResult<Decimal> divide(const Decimal& other) const;

    /// @brief Round to a smaller fractional scale using Half-Even while
    /// retaining Decimal's internal scale-10 representation.
    [[nodiscard]] DomainResult<Decimal> round_to_scale(
        std::int32_t fractional_digits) const;

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

    /// @brief True if this value fits the DB amount column NUMERIC(20, 8):
    /// at most 8 fractional digits and total significant digits <= 20 (so the
    /// integer part is <= 12 digits). Decimal's internal scale (10) and 128-bit
    /// range are wider than the database, so the persistence layer must reject
    /// values that would silently round or overflow on write. This is the
    /// single definition of the amount boundary, shared by any repository.
    [[nodiscard]] bool fits_numeric_20_8() const noexcept;

    /// @brief True if this value fits a rate column NUMERIC(20, 10): at most 10
    /// fractional digits and total significant digits <= 20.
    [[nodiscard]] bool fits_numeric_20_10() const noexcept;

private:
    explicit constexpr Decimal(StorageType scaled) noexcept : value_(scaled) {}

    StorageType value_; // real value * 10^kScale
};

} // namespace pfh::domain
