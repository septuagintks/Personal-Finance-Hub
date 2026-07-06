// Personal Finance Hub - ExchangeRate Value Object Implementation
// Version: 1.0
// C++23

#include "pfh/domain/exchange_rate.h"

namespace pfh::domain {

DomainResult<ExchangeRate> ExchangeRate::create(
    Currency base, Currency target, Decimal rate,
    TimePoint fetched_at, std::string source) {
    // Direction must be meaningful: base and target differ.
    if (base == target) {
        return std::unexpected(DomainError::invalid_exchange_rate(
            "base and target must differ: both are " + base.code()));
    }
    // Rate must be strictly positive; zero/negative rates are never valid.
    if (!rate.is_positive()) {
        return std::unexpected(DomainError::invalid_exchange_rate(
            "rate must be positive, got " + rate.to_string()));
    }
    return ExchangeRate(std::move(base), std::move(target), rate,
                        fetched_at, std::move(source));
}

DomainResult<ExchangeRate> ExchangeRate::inverse() const {
    // 1 / rate, with Decimal's Half-Even rounding at scale 10.
    auto one = Decimal::from_integer(1);
    if (!one) {
        return std::unexpected(one.error());
    }
    auto inv = one->divide(rate_);
    if (!inv) {
        return std::unexpected(inv.error());
    }
    return ExchangeRate::create(target_, base_, *inv, fetched_at_,
                                source_ + "+inverse");
}

DomainResult<Money> ExchangeRate::convert(const Money& amount) const {
    // The input must be denominated in the base currency.
    if (!(amount.currency() == base_)) {
        return std::unexpected(DomainError::currency_mismatch(
            base_.code(), amount.currency().code()));
    }
    auto converted = amount.amount().multiply(rate_);
    if (!converted) {
        return std::unexpected(converted.error());
    }
    return Money(*converted, target_);
}

} // namespace pfh::domain
