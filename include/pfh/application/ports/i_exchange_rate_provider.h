// Personal Finance Hub - Exchange Rate Provider Port
// Version: 1.0
// C++23
//
// Application-facing port for external rate fetch. Infrastructure implements
// real providers; tests use a mock.

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/exchange_rate.h"
#include "pfh/domain/repositories/repository_error.h"
#include <vector>

namespace pfh::application {

class IExchangeRateProvider {
public:
    virtual ~IExchangeRateProvider() = default;

    /// @brief Fetch latest rates for base(USD) -> each target.
    [[nodiscard]] virtual domain::RepositoryResult<std::vector<domain::ExchangeRate>>
    fetch_latest(
        const domain::Currency& base,
        const std::vector<domain::Currency>& targets) = 0;
};

} // namespace pfh::application
