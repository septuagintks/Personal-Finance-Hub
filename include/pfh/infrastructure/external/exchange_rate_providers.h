// Personal Finance Hub - External Exchange-Rate Providers

#pragma once

#include "pfh/application/ports/i_clock.h"
#include "pfh/application/ports/i_exchange_rate_provider.h"
#include "pfh/infrastructure/external/i_http_transport.h"

#include <chrono>
#include <string>
#include <utility>

namespace pfh::infrastructure {

class FreeCurrencyApiProvider final
    : public application::IExchangeRateProvider {
public:
    FreeCurrencyApiProvider(
        IHttpTransport& transport,
        const application::IClock& clock,
        std::string api_key,
        std::chrono::milliseconds timeout = std::chrono::seconds(10))
        : transport_(transport),
          clock_(clock),
          api_key_(std::move(api_key)),
          timeout_(timeout) {}
    ~FreeCurrencyApiProvider() override;

    FreeCurrencyApiProvider(const FreeCurrencyApiProvider&) = delete;
    FreeCurrencyApiProvider& operator=(const FreeCurrencyApiProvider&) = delete;

    [[nodiscard]] std::string_view provider_name() const noexcept override {
        return "FreeCurrencyAPI";
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

class ExchangeRateFunProvider final
    : public application::IExchangeRateProvider {
public:
    ExchangeRateFunProvider(
        IHttpTransport& transport,
        const application::IClock& clock,
        std::chrono::milliseconds timeout = std::chrono::seconds(10))
        : transport_(transport), clock_(clock), timeout_(timeout) {}

    [[nodiscard]] std::string_view provider_name() const noexcept override {
        return "exchangerate.fun";
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::ExchangeRate>>
    fetch_latest(
        const domain::Currency& base,
        const std::vector<domain::Currency>& targets) override;

private:
    IHttpTransport& transport_;
    const application::IClock& clock_;
    std::chrono::milliseconds timeout_;
};

class FailoverExchangeRateProvider final
    : public application::IExchangeRateProvider {
public:
    FailoverExchangeRateProvider(
        application::IExchangeRateProvider& primary,
        application::IExchangeRateProvider& fallback)
        : primary_(primary), fallback_(fallback) {}

    [[nodiscard]] std::string_view provider_name() const noexcept override {
        return "FreeCurrencyAPI/exchangerate.fun";
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::ExchangeRate>>
    fetch_latest(
        const domain::Currency& base,
        const std::vector<domain::Currency>& targets) override;

private:
    application::IExchangeRateProvider& primary_;
    application::IExchangeRateProvider& fallback_;
};

} // namespace pfh::infrastructure
