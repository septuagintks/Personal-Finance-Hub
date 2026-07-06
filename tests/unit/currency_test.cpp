// Personal Finance Hub - Currency Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/currency.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

namespace {
Currency make(std::string_view code) {
    auto r = Currency::create(code);
    EXPECT_TRUE(r.has_value()) << "Failed to create currency: " << code;
    return r.value_or(Currency::create("USD").value());
}
} // namespace

// ---- Valid creation ----

TEST(Currency, WhenValidFiatCode_Creates) {
    auto r = Currency::create("USD");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->code(), "USD");
}

TEST(Currency, WhenLowercaseCode_UpcasesToCanonical) {
    auto r = Currency::create("cny");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->code(), "CNY");
}

TEST(Currency, WhenMixedCaseCode_Upcases) {
    auto r = Currency::create("eUr");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->code(), "EUR");
}

TEST(Currency, WhenCryptoWhitelisted_Creates) {
    auto r = Currency::create("BTC");
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->is_crypto());
}

TEST(Currency, WhenFiat_IsNotCrypto) {
    EXPECT_FALSE(make("USD").is_crypto());
    EXPECT_FALSE(make("JPY").is_crypto());
}

// ---- Invalid creation ----

TEST(Currency, WhenTooShort_ReturnsError) {
    auto r = Currency::create("US");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::InvalidCurrency);
}

TEST(Currency, WhenTooLong_ReturnsError) {
    auto r = Currency::create("USDD");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::InvalidCurrency);
}

TEST(Currency, WhenContainsDigits_ReturnsError) {
    auto r = Currency::create("US1");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::InvalidCurrency);
}

TEST(Currency, WhenUnsupportedCode_ReturnsError) {
    auto r = Currency::create("XYZ");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::InvalidCurrency);
}

TEST(Currency, WhenEmpty_ReturnsError) {
    auto r = Currency::create("");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::InvalidCurrency);
}

// ---- Comparison ----

TEST(Currency, WhenSameCode_AreEqual) {
    EXPECT_EQ(make("USD"), make("USD"));
}

TEST(Currency, WhenDifferentCode_AreNotEqual) {
    EXPECT_NE(make("USD"), make("CNY"));
}

TEST(Currency, WhenLowercaseVsUppercase_AreEqualAfterNormalization) {
    EXPECT_EQ(make("usd"), make("USD"));
}

// ---- Pivot ----

TEST(Currency, PivotCode_IsUSD) {
    EXPECT_EQ(Currency::pivot_code(), "USD");
}

} // namespace pfh::test
