// Personal Finance Hub - Money Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/money.h"
#include "test_support.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

// ---- Construction & accessors ----

TEST(Money, WhenConstructed_ExposesAmountAndCurrency) {
    auto m = money("100.50", "USD");
    EXPECT_EQ(m.amount().to_string(), "100.5");
    EXPECT_EQ(m.currency().code(), "USD");
}

TEST(Money, WhenToString_FormatsAmountAndCode) {
    EXPECT_EQ(money("12.34", "USD").to_string(), "12.34 USD");
}

// ---- Same-currency arithmetic ----

TEST(Money, WhenAddingSameCurrency_ReturnsSum) {
    auto r = money("100", "USD").add(money("50", "USD"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->to_string(), "150 USD");
}

TEST(Money, WhenSubtractingSameCurrency_ReturnsDifference) {
    auto r = money("100", "USD").subtract(money("30", "USD"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->to_string(), "70 USD");
}

TEST(Money, WhenSubtractingToNegative_AllowsNegativeBalance) {
    auto r = money("10", "USD").subtract(money("25", "USD"));
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->is_negative());
    EXPECT_EQ(r->to_string(), "-15 USD");
}

// ---- Cross-currency arithmetic is forbidden ----

TEST(Money, WhenAddingDifferentCurrency_ReturnsError) {
    auto r = money("100", "USD").add(money("100", "CNY"));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::CurrencyMismatch);
}

TEST(Money, WhenSubtractingDifferentCurrency_ReturnsError) {
    auto r = money("100", "USD").subtract(money("100", "EUR"));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::CurrencyMismatch);
}

// ---- Scalar multiply keeps currency ----

TEST(Money, WhenMultipliedByFactor_ScalesAmountKeepsCurrency) {
    auto r = money("1000", "USD").multiply(dec("7.18"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->to_string(), "7180 USD");
}

// ---- Predicates & negate ----

TEST(Money, WhenZeroAmount_IsZero) {
    EXPECT_TRUE(money("0", "USD").is_zero());
}

TEST(Money, WhenNegated_FlipsSignKeepsCurrency) {
    auto n = money("42.5", "USD").negated();
    EXPECT_EQ(n.to_string(), "-42.5 USD");
}

// ---- Equality & comparison ----

TEST(Money, WhenSameAmountAndCurrency_AreEqual) {
    EXPECT_EQ(money("42.00", "USD"), money("42", "USD"));
}

TEST(Money, WhenDifferentCurrencySameAmount_AreNotEqual) {
    EXPECT_NE(money("42", "USD"), money("42", "CNY"));
}

TEST(Money, WhenComparingSameCurrency_OrdersCorrectly) {
    auto r = money("10", "USD").compare(money("20", "USD"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, std::strong_ordering::less);
}

TEST(Money, WhenComparingDifferentCurrency_ReturnsError) {
    auto r = money("10", "USD").compare(money("20", "CNY"));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::CurrencyMismatch);
}

} // namespace pfh::test
