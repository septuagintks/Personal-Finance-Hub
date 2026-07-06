// Personal Finance Hub - CurrencyConversionService Implementation
// Version: 1.0
// C++23

#include "pfh/domain/currency_conversion_service.h"
#include <algorithm>

namespace pfh::domain {

DomainResult<ExchangeRate> CurrencyConversionService::cross_rate(
    const ExchangeRate& pivot_to_base,
    const ExchangeRate& pivot_to_target) {
    const std::string pivot(Currency::pivot_code());

    // Both rates must be anchored on the USD pivot as their base.
    if (pivot_to_base.base().code() != pivot ||
        pivot_to_target.base().code() != pivot) {
        return std::unexpected(DomainError::invalid_exchange_rate(
            "cross_rate requires both inputs based on pivot " + pivot));
    }

    // base -> target = r_target / r_base
    auto cross = pivot_to_target.rate().divide(pivot_to_base.rate());
    if (!cross) {
        return std::unexpected(cross.error());
    }

    // Use the later of the two timestamps for the derived rate.
    const auto fetched_at =
        std::max(pivot_to_base.fetched_at(), pivot_to_target.fetched_at());

    // Derived direction: base (target of pivot_to_base) -> target (target of pivot_to_target).
    return ExchangeRate::create(
        pivot_to_base.target(),
        pivot_to_target.target(),
        *cross,
        fetched_at,
        "TriangularCalculation");
}

DomainResult<Money> CurrencyConversionService::convert(
    const Money& amount, const ExchangeRate& rate) {
    return rate.convert(amount);
}

} // namespace pfh::domain
