// Personal Finance Hub - External Exchange-Rate Provider Tests

#include "pfh/infrastructure/external/exchange_rate_providers.h"

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

TEST(FreeCurrencyApiProviderTest,
     ValidResponsePreservesNumericLexemesAndExactRequestedSet) {
    FakeHttpTransport transport;
    transport.result = HttpTransportResponse{
        200,
        R"({"data":{"EUR":0.9234567890,"CNY":7.1234567890}})"};
    ProviderClock clock;
    FreeCurrencyApiProvider provider(
        transport, clock, "secret+key", 2500ms);

    auto result = provider.fetch_latest(
        currency("USD"), {currency("EUR"), currency("CNY")});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[0].target().code(), "CNY");
    EXPECT_EQ((*result)[0].rate().to_string(), "7.123456789");
    EXPECT_EQ((*result)[1].target().code(), "EUR");
    EXPECT_EQ((*result)[1].rate().to_string(), "0.923456789");
    EXPECT_EQ((*result)[0].source(), "FreeCurrencyAPI");
    EXPECT_EQ((*result)[0].fetched_at(), clock.now());
    EXPECT_EQ(
        transport.last_path,
        "/v1/latest?apikey=secret%2Bkey&base_currency=USD&currencies=CNY%2CEUR");
    EXPECT_EQ(transport.last_timeout, 2500ms);
}

TEST(FreeCurrencyApiProviderTest, EmptyTargetsDoNotCallTransport) {
    FakeHttpTransport transport;
    ProviderClock clock;
    FreeCurrencyApiProvider provider(transport, clock, "secret");

    auto result = provider.fetch_latest(currency("USD"), {});

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
    EXPECT_EQ(transport.calls, 0);
}

TEST(FreeCurrencyApiProviderTest, RejectsMalformedOrInexactData) {
    ProviderClock clock;
    for (const auto& body : std::vector<std::string>{
             R"({"data":{"CNY":7.1,"CNY":7.2,"EUR":0.9}})",
             R"({"data":{"CNY":7.1}})",
             R"({"data":{"CNY":7.1,"EUR":0.9,"JPY":150}})",
             R"({"data":{"CNY":"7.1","EUR":0.9}})",
             R"({"data":{"CNY":10000000000,"EUR":0.9}})",
             R"({"data":[]})"}) {
        FakeHttpTransport transport;
        transport.result = HttpTransportResponse{200, body};
        FreeCurrencyApiProvider provider(transport, clock, "secret");

        auto result = provider.fetch_latest(
            currency("USD"), {currency("CNY"), currency("EUR")});

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(
            result.error().status,
            domain::RepositoryStatus::ValidationError);
    }
}

TEST(FreeCurrencyApiProviderTest,
     ExplicitlyRoundsProviderPrecisionToNumeric20_10UsingHalfEven) {
    FakeHttpTransport transport;
    transport.result = HttpTransportResponse{
        200,
        R"({"data":{"CNY":7.12345678905,"EUR":0.92345678915}})"};
    ProviderClock clock;
    FreeCurrencyApiProvider provider(transport, clock, "secret");

    auto result = provider.fetch_latest(
        currency("USD"), {currency("CNY"), currency("EUR")});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[0].rate().to_string(), "7.123456789");
    EXPECT_EQ((*result)[1].rate().to_string(), "0.9234567892");
    EXPECT_TRUE((*result)[0].rate().fits_numeric_20_10());
    EXPECT_TRUE((*result)[1].rate().fits_numeric_20_10());
}

TEST(FreeCurrencyApiProviderTest, TransportAndHttpFailuresAreRetryable) {
    ProviderClock clock;
    FakeHttpTransport transport;
    transport.result = std::unexpected(HttpTransportError{
        HttpTransportErrorKind::Timeout, "timeout detail"});
    FreeCurrencyApiProvider provider(transport, clock, "secret");

    auto timeout = provider.fetch_latest(
        currency("USD"), {currency("CNY")});
    ASSERT_FALSE(timeout.has_value());
    EXPECT_EQ(timeout.error().status, domain::RepositoryStatus::DatabaseError);
    EXPECT_EQ(timeout.error().message, "FreeCurrencyAPI request timed out");

    transport.result = HttpTransportResponse{429, R"({"message":"limit"})"};
    auto rate_limited = provider.fetch_latest(
        currency("USD"), {currency("CNY")});
    ASSERT_FALSE(rate_limited.has_value());
    EXPECT_EQ(
        rate_limited.error().status,
        domain::RepositoryStatus::DatabaseError);
}

TEST(ExchangeRateFunProviderTest,
     ValidSupersetReturnsOnlyRequestedCurrencies) {
    FakeHttpTransport transport;
    transport.result = HttpTransportResponse{
        200,
        R"({"timestamp":1700000000,"base":"USD","rates":{"USD":1,"EUR":0.9234567890,"CNY":7.1234567890,"JPY":150.5}})"};
    ProviderClock clock;
    ExchangeRateFunProvider provider(transport, clock, 1500ms);

    auto result = provider.fetch_latest(
        currency("USD"), {currency("EUR"), currency("CNY")});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[0].target().code(), "CNY");
    EXPECT_EQ((*result)[1].target().code(), "EUR");
    EXPECT_EQ((*result)[0].source(), "exchangerate.fun");
    EXPECT_EQ(
        (*result)[0].fetched_at(),
        std::chrono::system_clock::time_point{1700000000s});
    EXPECT_EQ(
        transport.last_path,
        "/latest?base=USD&symbols=CNY%2CEUR");
    EXPECT_EQ(transport.last_timeout, 1500ms);
}

TEST(ExchangeRateFunProviderTest, RejectsMissingOrInvalidRequiredData) {
    ProviderClock clock;
    for (const auto& body : std::vector<std::string>{
             R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":7.1}})",
             R"({"timestamp":1700000000,"base":"EUR","rates":{"CNY":7.1,"EUR":0.9}})",
             R"({"timestamp":1700000000.5,"base":"USD","rates":{"CNY":7.1,"EUR":0.9}})",
             R"({"timestamp":1900000000,"base":"USD","rates":{"CNY":7.1,"EUR":0.9}})",
             R"({"timestamp":253402300799,"base":"USD","rates":{"CNY":7.1,"EUR":0.9}})",
             R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":7.1,"CNY":7.2,"EUR":0.9}})",
             R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":0,"EUR":0.9}})"}) {
        FakeHttpTransport transport;
        transport.result = HttpTransportResponse{200, body};
        ExchangeRateFunProvider provider(transport, clock);

        auto result = provider.fetch_latest(
            currency("USD"), {currency("CNY"), currency("EUR")});

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(
            result.error().status,
            domain::RepositoryStatus::ValidationError);
    }
}

TEST(ExchangeRateFunProviderTest,
     AcceptsHighPrecisionFeedTokenAfterExplicitHalfEvenNormalization) {
    FakeHttpTransport transport;
    transport.result = HttpTransportResponse{
        200,
        R"({"timestamp":1700000000,"base":"USD","rates":{"BTC":0.000015123456}})"};
    ProviderClock clock;
    ExchangeRateFunProvider provider(transport, clock);

    auto result = provider.fetch_latest(
        currency("USD"), {currency("BTC")});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].rate().to_string(), "0.0000151235");
    EXPECT_TRUE((*result)[0].rate().fits_numeric_20_10());
}

TEST(FailoverExchangeRateProviderTest,
     PrimarySuccessDoesNotCallFallback) {
    ProviderClock clock;
    FakeHttpTransport primary_transport;
    primary_transport.result = HttpTransportResponse{
        200, R"({"data":{"CNY":7.1}})"};
    FakeHttpTransport fallback_transport;
    fallback_transport.result = HttpTransportResponse{
        200,
        R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":7.2}})"};
    FreeCurrencyApiProvider primary(primary_transport, clock, "secret");
    ExchangeRateFunProvider fallback(fallback_transport, clock);
    FailoverExchangeRateProvider provider(primary, fallback);

    auto result = provider.fetch_latest(
        currency("USD"), {currency("CNY")});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].rate().to_string(), "7.1");
    EXPECT_EQ((*result)[0].source(), "FreeCurrencyAPI");
    EXPECT_EQ(primary_transport.calls, 1);
    EXPECT_EQ(fallback_transport.calls, 0);
}

TEST(FailoverExchangeRateProviderTest,
     AnyPrimaryBatchFailureUsesExchangeRateFunForTheWholeBatch) {
    ProviderClock clock;
    FakeHttpTransport primary_transport;
    primary_transport.result = HttpTransportResponse{
        200, R"({"data":{"EUR":0.9}})"};
    FakeHttpTransport fallback_transport;
    fallback_transport.result = HttpTransportResponse{
        200,
        R"({"timestamp":1700000000,"base":"USD","rates":{"USD":1,"CNY":7.2,"EUR":0.9}})"};
    FreeCurrencyApiProvider primary(primary_transport, clock, "secret");
    ExchangeRateFunProvider fallback(fallback_transport, clock);
    FailoverExchangeRateProvider provider(primary, fallback);

    auto result = provider.fetch_latest(
        currency("USD"), {currency("CNY"), currency("EUR")});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[0].rate().to_string(), "7.2");
    EXPECT_EQ((*result)[0].source(), "exchangerate.fun");
    EXPECT_EQ((*result)[1].rate().to_string(), "0.9");
    EXPECT_EQ((*result)[1].source(), "exchangerate.fun");
    EXPECT_EQ(primary_transport.calls, 1);
    EXPECT_EQ(fallback_transport.calls, 1);
}

TEST(FailoverExchangeRateProviderTest,
     PrimaryTimeoutUsesExchangeRateFun) {
    ProviderClock clock;
    FakeHttpTransport primary_transport;
    primary_transport.result = std::unexpected(HttpTransportError{
        HttpTransportErrorKind::Timeout, "timeout"});
    FakeHttpTransport fallback_transport;
    fallback_transport.result = HttpTransportResponse{
        200,
        R"({"timestamp":1700000000,"base":"USD","rates":{"CNY":7.2}})"};
    FreeCurrencyApiProvider primary(primary_transport, clock, "secret");
    ExchangeRateFunProvider fallback(fallback_transport, clock);
    FailoverExchangeRateProvider provider(primary, fallback);

    auto result = provider.fetch_latest(
        currency("USD"), {currency("CNY")});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ((*result)[0].source(), "exchangerate.fun");
    EXPECT_EQ(primary_transport.calls, 1);
    EXPECT_EQ(fallback_transport.calls, 1);
}

TEST(FailoverExchangeRateProviderTest,
     BothFailuresReturnOneSanitizedRetryableError) {
    ProviderClock clock;
    FakeHttpTransport primary_transport;
    primary_transport.result = HttpTransportResponse{503, "primary detail"};
    FakeHttpTransport fallback_transport;
    fallback_transport.result = std::unexpected(HttpTransportError{
        HttpTransportErrorKind::Network, "fallback detail"});
    FreeCurrencyApiProvider primary(primary_transport, clock, "secret");
    ExchangeRateFunProvider fallback(fallback_transport, clock);
    FailoverExchangeRateProvider provider(primary, fallback);

    auto result = provider.fetch_latest(
        currency("USD"), {currency("CNY")});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, domain::RepositoryStatus::DatabaseError);
    EXPECT_EQ(
        result.error().message,
        "All configured exchange-rate providers failed");
    EXPECT_EQ(provider.provider_name(), "FreeCurrencyAPI/exchangerate.fun");
}

TEST(FailoverExchangeRateProviderTest,
     InvalidRequestDoesNotCallEitherProvider) {
    ProviderClock clock;
    FakeHttpTransport primary_transport;
    FakeHttpTransport fallback_transport;
    FreeCurrencyApiProvider primary(primary_transport, clock, "secret");
    ExchangeRateFunProvider fallback(fallback_transport, clock);
    FailoverExchangeRateProvider provider(primary, fallback);

    auto result = provider.fetch_latest(
        currency("USD"), {currency("USD")});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, domain::RepositoryStatus::ValidationError);
    EXPECT_EQ(primary_transport.calls, 0);
    EXPECT_EQ(fallback_transport.calls, 0);
}

} // namespace
} // namespace pfh::infrastructure
