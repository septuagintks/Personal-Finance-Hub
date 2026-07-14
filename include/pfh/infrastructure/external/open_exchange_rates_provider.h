// Personal Finance Hub - OpenExchangeRates Provider

#pragma once

#include "pfh/application/ports/i_clock.h"
#include "pfh/application/ports/i_exchange_rate_provider.h"
#include "pfh/infrastructure/external/i_http_transport.h"

#include <chrono>
#include <string>
#include <utility>

namespace pfh::infrastructure {

class OpenExchangeRatesProvider final
    : public application::IExchangeRateProvider {
public:
    OpenExchangeRatesProvider(
        IHttpTransport& transport,
        const application::IClock& clock,
        std::string api_key,
        std::chrono::milliseconds timeout = std::chrono::seconds(10))
        : transport_(transport),
          clock_(clock),
          api_key_(std::move(api_key)),
          timeout_(timeout) {}
    ~OpenExchangeRatesProvider() override;

    OpenExchangeRatesProvider(const OpenExchangeRatesProvider&) = delete;
    OpenExchangeRatesProvider& operator=(const OpenExchangeRatesProvider&) = delete;

    [[nodiscard]] std::string_view provider_name() const noexcept override {
        return "OpenExchangeRates";
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::ExchangeRate>>
    fetch_latest(
        const domain::Currency& base,
        const std::vector<domain::Currency>& targets) override;

private:
    IHttpTransport& transport_;
    const application::IClock& clock_;
    std::string api_key_;
    std::chrono::milliseconds timeout_;
};

} // namespace pfh::infrastructure
