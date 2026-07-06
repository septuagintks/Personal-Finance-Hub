// Personal Finance Hub - Money Value Object
// Version: 1.0
// C++23
//
// Money binds a Decimal amount to a Currency. It is immutable. Arithmetic and
// comparison require matching currencies; cross-currency operations fail with
// a DomainError (they must go through CurrencyConversionService explicitly).

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/decimal.h"
#include "pfh/domain/domain_error.h"
#include <string>

namespace pfh::domain {

/// @brief Immutable monetary value: an amount bound to a currency.
class Money {
public:
    Money(Decimal amount, Currency currency)
        : amount_(amount), currency_(std::move(currency)) {}

    [[nodiscard]] const Decimal& amount() const noexcept { return amount_; }
    [[nodiscard]] const Currency& currency() const noexcept { return currency_; }

    [[nodiscard]] bool is_zero() const noexcept { return amount_.is_zero(); }
    [[nodiscard]] bool is_negative() const noexcept { return amount_.is_negative(); }
    [[nodiscard]] bool is_positive() const noexcept { return amount_.is_positive(); }

    /// @brief Negate the amount, keeping the same currency.
    [[nodiscard]] Money negated() const { return Money(amount_.negated(), currency_); }

    /// @brief Add another Money. Requires the same currency.
    [[nodiscard]] DomainResult<Money> add(const Money& other) const;

    /// @brief Subtract another Money. Requires the same currency.
    [[nodiscard]] DomainResult<Money> subtract(const Money& other) const;

    /// @brief Multiply the amount by a scalar Decimal (e.g. a rate/factor).
    /// The currency is unchanged. Use CurrencyConversionService for cross-currency.
    [[nodiscard]] DomainResult<Money> multiply(const Decimal& factor) const;

    /// @brief Equality requires the same currency AND amount.
    [[nodiscard]] bool operator==(const Money& other) const noexcept {
        return currency_ == other.currency_ && amount_ == other.amount_;
    }

    /// @brief Ordered comparison. Requires matching currency; returns error otherwise.
    [[nodiscard]] DomainResult<std::strong_ordering> compare(const Money& other) const;

    /// @brief Canonical string form: "<amount> <CODE>", e.g. "12.34 USD".
    [[nodiscard]] std::string to_string() const {
        return amount_.to_string() + " " + currency_.code();
    }

private:
    Decimal amount_;
    Currency currency_;
};

} // namespace pfh::domain
