// Personal Finance Hub - ExchangeRate Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/exchange_rate.h"
#include "test_support.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

// ---- Creation ----

TEST(ExchangeRate, WhenValid_Creates) {
    auto er = ExchangeRate::create(ccy("USD"), ccy("CNY"), dec("7.18"), sample_time(), "ECB");
    ASSERT_TRUE(er.has_value());
    EXPECT_EQ(er->base().code(), "USD");
    EXPECT_EQ(er->target().code(), "CNY");
    EXPECT_EQ(er->rate().to_string(), "7.18");
    EXPECT_EQ(er->source(), "ECB");
}

TEST(ExchangeRate, WhenBaseEqualsTarget_ReturnsError) {
    auto er = ExchangeRate::create(ccy("USD"), ccy("USD"), dec("1"), sample_time(), "X");
    ASSERT_FALSE(er.has_value());
    EXPECT_EQ(er.error().code, DomainErrorCode::InvalidExchangeRate);
}

TEST(ExchangeRate, WhenRateZero_ReturnsError) {
    auto er = ExchangeRate::create(ccy("USD"), ccy("CNY"), dec("0"), sample_time(), "X");
    ASSERT_FALSE(er.has_value());
    EXPECT_EQ(er.error().code, DomainErrorCode::InvalidExchangeRate);
}

TEST(ExchangeRate, WhenRateNegative_ReturnsError) {
    auto er = ExchangeRate::create(ccy("USD"), ccy("CNY"), dec("-7.18"), sample_time(), "X");
    ASSERT_FALSE(er.has_value());
    EXPECT_EQ(er.error().code, DomainErrorCode::InvalidExchangeRate);
}

// ---- Inverse ----

TEST(ExchangeRate, WhenInverted_SwapsDirectionAndReciprocatesRate) {
    auto er = rate("USD", "CNY", "8");   // 1 USD = 8 CNY
    auto inv = er.inverse();
    ASSERT_TRUE(inv.has_value());
    EXPECT_EQ(inv->base().code(), "CNY");
    EXPECT_EQ(inv->target().code(), "USD");
    EXPECT_EQ(inv->rate().to_string(), "0.125"); // 1/8
}

TEST(ExchangeRate, WhenInvertedTwice_ApproximatesOriginal) {
    auto er = rate("USD", "CNY", "8");
    auto inv = er.inverse();
    ASSERT_TRUE(inv.has_value());
    auto back = inv->inverse();
    ASSERT_TRUE(back.has_value());
    // 1 / (1/8) = 8 exactly for this value
    EXPECT_EQ(back->rate().to_string(), "8");
}

TEST(ExchangeRate, WhenInvertedPreservesTimestamp) {
    auto er = rate("USD", "CNY", "7.18");
    auto inv = er.inverse();
    ASSERT_TRUE(inv.has_value());
    EXPECT_EQ(inv->fetched_at(), er.fetched_at());
}

// ---- Convert ----

TEST(ExchangeRate, WhenConvertingBaseCurrency_ProducesTargetMoney) {
    auto er = rate("USD", "CNY", "7.18");
    auto usd = Money(dec("1000"), ccy("USD"));
    auto result = er.convert(usd);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "7180 CNY");
}

TEST(ExchangeRate, WhenConvertingWrongCurrency_ReturnsError) {
    auto er = rate("USD", "CNY", "7.18");
    auto cny = Money(dec("1000"), ccy("CNY")); // wrong: not the base
    auto result = er.convert(cny);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::CurrencyMismatch);
}

TEST(ExchangeRate, WhenConversionOverflows_PropagatesError) {
    // Keep the rate inside NUMERIC(20,10), then use a huge domain amount so the
    // multiplication itself overflows Decimal. convert() must surface that error.
    auto er = rate("USD", "CNY", "9999999999.9999999999");
    auto usd = Money(dec("99999999999999999999"), ccy("USD"));
    auto result = er.convert(usd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::Overflow);
}

TEST(ExchangeRate, WhenDoubleInverse_RestoresSourceLabel) {
    auto er = rate("USD", "CNY", "8", 1719302400, "ECB");
    auto inv = er.inverse();
    ASSERT_TRUE(inv.has_value());
    EXPECT_EQ(inv->source(), "ECB+inverse");
    auto back = inv->inverse();
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->source(), "ECB"); // suffix toggled off, not accumulated
}

} // namespace pfh::test
