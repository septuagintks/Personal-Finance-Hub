// Personal Finance Hub - Currency Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/currency.h"
#include "test_support.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

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

TEST(Currency, WhenFourLetterCrypto_Creates) {
    // Regression: USDT is 4 letters; must not be rejected by a 3-letter shape rule.
    auto r = Currency::create("USDT");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->code(), "USDT");
    EXPECT_TRUE(r->is_crypto());
}

TEST(Currency, WhenFiveLetterCrypto_Creates) {
    auto r = Currency::create("WBTC");
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->is_crypto());
}

TEST(Currency, WhenLowercaseFourLetterCrypto_Upcases) {
    auto r = Currency::create("usdc");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->code(), "USDC");
    EXPECT_TRUE(r->is_crypto());
}

TEST(Currency, WhenSixLetters_ReturnsError) {
    auto r = Currency::create("TOOLONG");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::InvalidCurrency);
}

TEST(Currency, WhenUnknownFourLetter_ReturnsError) {
    // 4-letter but not in the crypto whitelist -> rejected.
    auto r = Currency::create("ABCD");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::InvalidCurrency);
}

TEST(Currency, WhenFiat_IsNotCrypto) {
    EXPECT_FALSE(ccy("USD").is_crypto());
    EXPECT_FALSE(ccy("JPY").is_crypto());
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
    EXPECT_EQ(ccy("USD"), ccy("USD"));
}

TEST(Currency, WhenDifferentCode_AreNotEqual) {
    EXPECT_NE(ccy("USD"), ccy("CNY"));
}

TEST(Currency, WhenLowercaseVsUppercase_AreEqualAfterNormalization) {
    EXPECT_EQ(ccy("usd"), ccy("USD"));
}

// ---- Pivot ----

TEST(Currency, PivotCode_IsUSD) {
    EXPECT_EQ(Currency::pivot_code(), "USD");
}

// ---- Item 10: domain whitelist stays in sync with the V2 currency seed ----

TEST(Currency, AcceptsV2SeededFiatAndCrypto) {
    // Fiat and crypto codes that are seeded by V2 must be accepted by the domain.
    for (const char* code : {"BRL", "MXN", "NOK", "RUB", "SEK"}) {
        EXPECT_TRUE(Currency::create(code).has_value()) << code;
    }
    for (const char* code : {"BNB", "XRP", "ADA", "DOGE", "SOL", "TRX", "MATIC", "DOT"}) {
        EXPECT_TRUE(Currency::create(code).has_value()) << code;
    }
}

TEST(Currency, RejectsCodesNotInV2Seed) {
    // These fiat codes are NOT seeded by V2, so the domain must reject them; an
    // account in such a currency could not be persisted (FK to currencies).
    for (const char* code : {"IDR", "MYR", "PHP", "THB", "VND"}) {
        EXPECT_FALSE(Currency::create(code).has_value()) << code;
    }
}

} // namespace pfh::test
