// Personal Finance Hub - ExchangeRate Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/exchange_rate.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

namespace {
Currency ccy(std::string_view code) { return Currency::create(code).value(); }
Decimal dec(std::string_view text) { return Decimal::parse(text).value(); }

ExchangeRate::TimePoint some_time() {
    return std::chrono::system_clock::from_time_t(1719302400); // 2024-06-25
}

ExchangeRate rate(std::string_view base, std::string_view target, std::string_view r) {
    auto er = ExchangeRate::create(ccy(base), ccy(target), dec(r), some_time(), "Test");
    EXPECT_TRUE(er.has_value());
    return er.value();
}
} // namespace

// ---- Creation ----

TEST(ExchangeRate, WhenValid_Creates) {
    auto er = ExchangeRate::create(ccy("USD"), ccy("CNY"), dec("7.18"), some_time(), "ECB");
    ASSERT_TRUE(er.has_value());
    EXPECT_EQ(er->base().code(), "USD");
    EXPECT_EQ(er->target().code(), "CNY");
    EXPECT_EQ(er->rate().to_string(), "7.18");
    EXPECT_EQ(er->source(), "ECB");
}

TEST(ExchangeRate, WhenBaseEqualsTarget_ReturnsError) {
    auto er = ExchangeRate::create(ccy("USD"), ccy("USD"), dec("1"), some_time(), "X");
    ASSERT_FALSE(er.has_value());
    EXPECT_EQ(er.error().code, DomainErrorCode::InvalidExchangeRate);
}

TEST(ExchangeRate, WhenRateZero_ReturnsError) {
    auto er = ExchangeRate::create(ccy("USD"), ccy("CNY"), dec("0"), some_time(), "X");
    ASSERT_FALSE(er.has_value());
    EXPECT_EQ(er.error().code, DomainErrorCode::InvalidExchangeRate);
}

TEST(ExchangeRate, WhenRateNegative_ReturnsError) {
    auto er = ExchangeRate::create(ccy("USD"), ccy("CNY"), dec("-7.18"), some_time(), "X");
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

} // namespace pfh::test
