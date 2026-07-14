// Personal Finance Hub - OpenExchangeRates Provider Tests

#include "pfh/infrastructure/external/open_exchange_rates_provider.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pfh::infrastructure {
namespace {

using namespace std::chrono_literals;

class ProviderClock final : public application::IClock {
public:
    [[nodiscard]] std::chrono::system_clock::time_point now() const override {
        return std::chrono::system_clock::time_point{1800000000s};
    }
};

class FakeHttpTransport final : public IHttpTransport {
public:
    [[nodiscard]] HttpTransportResult get(
        std::string_view path,
        std::chrono::milliseconds timeout) override {
        last_path = std::string(path);
        last_timeout = timeout;
        ++calls;
        return result;
    }

    HttpTransportResult result = HttpTransportResponse{200, "{}"};
    std::string last_path;
    std::chrono::milliseconds last_timeout{};
    int calls = 0;
};

[[nodiscard]] domain::Currency currency(std::string_view code) {
    auto result = domain::Currency::create(code);
    EXPECT_TRUE(result.has_value());
    return *result;
}

TEST(OpenExchangeRatesProviderTest,
     ValidResponsePreservesNumericLexemesAndExactRequestedSet) {
    FakeHttpTransport transport;
    transport.result = HttpTransportResponse{
        200,
        R"({"disclaimer":"test","timestamp":1700000000,"base":"USD","rates":{"EUR":0.9234567890,"CNY":7.1234567890}})"};
    ProviderClock clock;
    OpenExchangeRatesProvider provider(
        transport, clock, "secret+key", 2500ms);

    auto result = provider.fetch_latest(
        currency("USD"), {currency("EUR"), currency("CNY")});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[0].target().code(), "CNY");
    EXPECT_EQ((*result)[0].rate().to_string(), "7.123456789");
    EXPECT_EQ((*result)[1].target().code(), "EUR");
    EXPECT_EQ((*result)[1].rate().to_string(), "0.923456789");
    EXPECT_EQ((*result)[0].source(), "OpenExchangeRates");
    EXPECT_EQ(
        (*result)[0].fetched_at(),
        std::chrono::system_clock::time_point{1700000000s});
    EXPECT_EQ(
        transport.last_path,
        "/api/latest.json?app_id=secret%2Bkey&symbols=CNY%2CEUR");
    EXPECT_EQ(transport.last_timeout, 2500ms);
}

TEST(OpenExchangeRatesProviderTest, EmptyTargetsDoNotCallTransport) {
    FakeHttpTransport transport;
    ProviderClock clock;
    OpenExchangeRatesProvider provider(transport, clock, "secret");

    auto result = provider.fetch_latest(currency("USD"), {});

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
    EXPECT_EQ(transport.calls, 0);
}

TEST(OpenExchangeRatesProviderTest, RejectsDuplicateRateKey) {
    FakeHttpTransport transport;
    transport.result = HttpTransportResponse{
        200,
        R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":7.1,"CNY":7.2}})"};
    ProviderClock clock;
    OpenExchangeRatesProvider provider(transport, clock, "secret");

    auto result = provider.fetch_latest(
        currency("USD"), {currency("CNY")});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, domain::RepositoryStatus::ValidationError);
}

TEST(OpenExchangeRatesProviderTest, RejectsMissingOrUnexpectedCurrency) {
    ProviderClock clock;
    for (const auto& body : std::vector<std::string>{
             R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":7.1}})",
             R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":7.1,"EUR":0.9,"JPY":150}})"}) {
        FakeHttpTransport transport;
        transport.result = HttpTransportResponse{200, body};
        OpenExchangeRatesProvider provider(transport, clock, "secret");

        auto result = provider.fetch_latest(
            currency("USD"), {currency("CNY"), currency("EUR")});

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(
            result.error().status,
            domain::RepositoryStatus::ValidationError);
    }
}

TEST(OpenExchangeRatesProviderTest, RejectsInvalidBaseTimestampAndRate) {
    ProviderClock clock;
    for (const auto& body : std::vector<std::string>{
             R"({"timestamp":1700000000,"base":"EUR","rates":{"CNY":7.1}})",
             R"({"timestamp":1700000000.5,"base":"USD","rates":{"CNY":7.1}})",
             R"({"timestamp":1900000000,"base":"USD","rates":{"CNY":7.1}})",
             R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":0}})",
             R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":"7.1"}})",
             R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":0.12345678901}})"}) {
        FakeHttpTransport transport;
        transport.result = HttpTransportResponse{200, body};
        OpenExchangeRatesProvider provider(transport, clock, "secret");

        auto result = provider.fetch_latest(
            currency("USD"), {currency("CNY")});

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(
            result.error().status,
            domain::RepositoryStatus::ValidationError);
    }
}

TEST(OpenExchangeRatesProviderTest, TransportAndHttpFailuresRemainRetryable) {
    ProviderClock clock;
    FakeHttpTransport transport;
    transport.result = std::unexpected(HttpTransportError{
        HttpTransportErrorKind::Timeout, "timeout detail"});
    OpenExchangeRatesProvider provider(transport, clock, "secret");

    auto timeout = provider.fetch_latest(
        currency("USD"), {currency("CNY")});
    ASSERT_FALSE(timeout.has_value());
    EXPECT_EQ(timeout.error().status, domain::RepositoryStatus::DatabaseError);
    EXPECT_EQ(timeout.error().message, "OpenExchangeRates request timed out");

    transport.result = HttpTransportResponse{429, R"({"error":true})"};
    auto rate_limited = provider.fetch_latest(
        currency("USD"), {currency("CNY")});
    ASSERT_FALSE(rate_limited.has_value());
    EXPECT_EQ(
        rate_limited.error().status,
        domain::RepositoryStatus::DatabaseError);
}

} // namespace
} // namespace pfh::infrastructure
