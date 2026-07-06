// Personal Finance Hub - CurrencyConversionService Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/currency_conversion_service.h"
#include "test_support.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

// ---- Triangular cross-rate via USD pivot ----

TEST(CurrencyConversionService, WhenPivotToBaseAndTarget_DerivesCrossRate) {
    // USD->EUR = 0.92, USD->CNY = 7.18  =>  EUR->CNY = 7.18 / 0.92
    auto usd_eur = rate("USD", "EUR", "0.92");
    auto usd_cny = rate("USD", "CNY", "7.18");

    auto cross = CurrencyConversionService::cross_rate(usd_eur, usd_cny);
    ASSERT_TRUE(cross.has_value());
    EXPECT_EQ(cross->base().code(), "EUR");
    EXPECT_EQ(cross->target().code(), "CNY");
    // 7.18 / 0.92 = 7.8043478261 (Half-Even at scale 10)
    EXPECT_EQ(cross->rate().to_string(), "7.8043478261");
    EXPECT_EQ(cross->source(), "TriangularCalculation");
}

TEST(CurrencyConversionService, WhenCrossRateExact_ComputesCleanly) {
    // USD->EUR = 0.5, USD->CNY = 8  =>  EUR->CNY = 8 / 0.5 = 16
    auto usd_eur = rate("USD", "EUR", "0.5");
    auto usd_cny = rate("USD", "CNY", "8");
    auto cross = CurrencyConversionService::cross_rate(usd_eur, usd_cny);
    ASSERT_TRUE(cross.has_value());
    EXPECT_EQ(cross->rate().to_string(), "16");
}

TEST(CurrencyConversionService, WhenCrossRate_UsesLaterTimestamp) {
    auto usd_eur = rate("USD", "EUR", "0.92", 1000);
    auto usd_cny = rate("USD", "CNY", "7.18", 2000);
    auto cross = CurrencyConversionService::cross_rate(usd_eur, usd_cny);
    ASSERT_TRUE(cross.has_value());
    EXPECT_EQ(cross->fetched_at(), time_at(2000)); // later of the two
}

TEST(CurrencyConversionService, WhenFirstInputNotPivotBased_ReturnsError) {
    // EUR->CNY as first input is not USD-based
    auto eur_cny = rate("EUR", "CNY", "7.8");
    auto usd_cny = rate("USD", "CNY", "7.18");
    auto cross = CurrencyConversionService::cross_rate(eur_cny, usd_cny);
    ASSERT_FALSE(cross.has_value());
    EXPECT_EQ(cross.error().code, DomainErrorCode::InvalidExchangeRate);
}

TEST(CurrencyConversionService, WhenSecondInputNotPivotBased_ReturnsError) {
    auto usd_eur = rate("USD", "EUR", "0.92");
    auto cny_jpy = rate("CNY", "JPY", "20");
    auto cross = CurrencyConversionService::cross_rate(usd_eur, cny_jpy);
    ASSERT_FALSE(cross.has_value());
    EXPECT_EQ(cross.error().code, DomainErrorCode::InvalidExchangeRate);
}

// ---- convert() convenience ----

TEST(CurrencyConversionService, WhenConvertingWithMatchingRate_ReturnsTargetMoney) {
    auto usd_cny = rate("USD", "CNY", "7.18");
    auto usd = Money(dec("1000"), ccy("USD"));
    auto result = CurrencyConversionService::convert(usd, usd_cny);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "7180 CNY");
}

TEST(CurrencyConversionService, WhenConvertingWithMismatchedRate_ReturnsError) {
    auto usd_cny = rate("USD", "CNY", "7.18");
    auto eur = Money(dec("1000"), ccy("EUR"));
    auto result = CurrencyConversionService::convert(eur, usd_cny);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::CurrencyMismatch);
}

// ---- Cross-rate then convert (end-to-end triangulation) ----

TEST(CurrencyConversionService, WhenTriangulatedThenConvert_ProducesExpectedAmount) {
    auto usd_eur = rate("USD", "EUR", "0.5");
    auto usd_cny = rate("USD", "CNY", "8");
    auto eur_cny = CurrencyConversionService::cross_rate(usd_eur, usd_cny);
    ASSERT_TRUE(eur_cny.has_value());

    auto eur = Money(dec("100"), ccy("EUR"));
    auto cny = CurrencyConversionService::convert(eur, *eur_cny);
    ASSERT_TRUE(cny.has_value());
    EXPECT_EQ(cny->to_string(), "1600 CNY"); // 100 * 16
}

} // namespace pfh::test
