// Personal Finance Hub - CurrencyConversionService (Domain Service)
// Version: 1.0
// C++23
//
// Pure in-memory currency conversion math. This service performs NO Repository
// access, opens NO transactions, and publishes NO events (per Clean
// Architecture rules). The Repository-backed fallback chain
// (direct -> inverse -> triangulation -> historical) is an application-layer
// concern and lives in a query service once IExchangeRateRepository exists
// (P1-S08). Here we expose the deterministic math those layers compose:
//   - convert(): apply a known rate to an amount
//   - cross_rate(): triangulate base->target via the USD pivot
//   - invert(): reverse a rate

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/exchange_rate.h"
#include "pfh/domain/money.h"

namespace pfh::domain {

/// @brief Stateless domain service for currency conversion arithmetic.
class CurrencyConversionService {
public:
    /// @brief Triangulate base -> target through the USD pivot.
    ///
    /// Given pivot->base (1 USD = r_base base) and pivot->target
    /// (1 USD = r_target target), derives base -> target = r_target / r_base.
    /// Both inputs MUST have the pivot (USD) as their base currency.
    ///
    /// @return The derived base->target ExchangeRate, tagged as triangulated
    ///         with the later of the two timestamps; error if the pivot
    ///         constraint is violated or the resulting rate is non-positive.
    [[nodiscard]] static DomainResult<ExchangeRate> cross_rate(
        const ExchangeRate& pivot_to_base,
        const ExchangeRate& pivot_to_target);

    /// @brief Convert Money using a rate whose base matches the amount's currency.
    /// Convenience wrapper around ExchangeRate::convert.
    [[nodiscard]] static DomainResult<Money> convert(
        const Money& amount, const ExchangeRate& rate);
};

} // namespace pfh::domain
