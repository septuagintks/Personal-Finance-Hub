// Personal Finance Hub - Currency Value Object
// Version: 1.0
// C++23
//
// Currency is an immutable value object identifying the unit of a monetary
// amount. It carries only the stable code (ISO-4217 for fiat, controlled
// whitelist for crypto). Display attributes (symbol, precision, name) live in
// CurrencyMetadata, not here. Currency supports NO arithmetic.

#pragma once

#include "pfh/domain/domain_error.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace pfh::domain {

/// @brief Immutable currency code value object.
///
/// The code is a stable 3-character identifier (e.g. "USD", "CNY", "BTC").
/// Validation accepts ISO-4217 fiat codes plus a controlled crypto whitelist.
class Currency {
public:
    /// @brief Create a Currency after validating the code.
    /// @param code 3-letter uppercase code (case-insensitive input is upcased).
    [[nodiscard]] static DomainResult<Currency> create(std::string_view code);

    /// @brief The stable currency code (uppercase, e.g. "USD").
    [[nodiscard]] const std::string& code() const noexcept { return code_; }

    /// @brief True if this is a whitelisted crypto currency code.
    [[nodiscard]] bool is_crypto() const noexcept;

    [[nodiscard]] bool operator==(const Currency& other) const noexcept {
        return code_ == other.code_;
    }
    [[nodiscard]] auto operator<=>(const Currency& other) const noexcept {
        return code_ <=> other.code_;
    }

    /// @brief The pivot currency used for triangular cross-rate calculation.
    [[nodiscard]] static std::string_view pivot_code() noexcept { return "USD"; }

private:
    explicit Currency(std::string code) : code_(std::move(code)) {}

    std::string code_;
};

} // namespace pfh::domain
