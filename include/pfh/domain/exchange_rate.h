// Personal Finance Hub - ExchangeRate Value Object
// Version: 1.0
// C++23
//
// ExchangeRate describes a directional conversion between two currencies at a
// point in time, from a named source. Convention: 1 base = rate target.
// It is a value object (no identity); uniqueness is base + target + timestamp.
// Historical rates are append-only at the persistence layer.

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/decimal.h"
#include "pfh/domain/domain_error.h"
#include "pfh/domain/money.h"
#include <chrono>
#include <string>

namespace pfh::domain {

/// @brief Directional exchange rate value object: 1 base = rate target.
class ExchangeRate {
public:
    using TimePoint = std::chrono::system_clock::time_point;

    /// @brief Create an ExchangeRate, validating direction and rate positivity.
    /// @param base   The base currency (left of "1 base = rate target").
    /// @param target The target currency.
    /// @param rate   Positive conversion factor. Must be > 0.
    /// @param fetched_at When this rate was observed.
    /// @param source Provider name ("FreeCurrencyAPI", "exchangerate.fun", ...).
    [[nodiscard]] static DomainResult<ExchangeRate> create(
        Currency base, Currency target, Decimal rate,
        TimePoint fetched_at, std::string source);

    [[nodiscard]] const Currency& base() const noexcept { return base_; }
    [[nodiscard]] const Currency& target() const noexcept { return target_; }
    [[nodiscard]] const Decimal& rate() const noexcept { return rate_; }
    [[nodiscard]] TimePoint fetched_at() const noexcept { return fetched_at_; }
    [[nodiscard]] const std::string& source() const noexcept { return source_; }

    /// @brief Reverse direction: target -> base, rate = 1 / rate.
    /// Keeps the same timestamp; source is tagged as inverse-derived.
    [[nodiscard]] DomainResult<ExchangeRate> inverse() const;

    /// @brief Convert an amount in the base currency to the target currency.
    /// The input Money must be in the base currency.
    [[nodiscard]] DomainResult<Money> convert(const Money& amount) const;

private:
    ExchangeRate(Currency base, Currency target, Decimal rate,
                 TimePoint fetched_at, std::string source)
        : base_(std::move(base)), target_(std::move(target)), rate_(rate),
          fetched_at_(fetched_at), source_(std::move(source)) {}

    Currency base_;
    Currency target_;
    Decimal rate_;
    TimePoint fetched_at_;
    std::string source_;
};

} // namespace pfh::domain
