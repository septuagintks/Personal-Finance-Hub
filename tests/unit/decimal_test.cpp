// Personal Finance Hub - Decimal Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/decimal.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

namespace {
// Helper: parse and unwrap, failing the test on parse error.
Decimal make(std::string_view text) {
    auto result = Decimal::parse(text);
    EXPECT_TRUE(result.has_value()) << "Failed to parse: " << text;
    return result.value_or(Decimal{});
}
} // namespace

// ---- Parsing: normal paths ----

TEST(Decimal, WhenParsingSimpleInteger_StoresCorrectly) {
    auto d = make("42");
    EXPECT_EQ(d.to_string(), "42");
}

TEST(Decimal, WhenParsingDecimal_StoresCorrectly) {
    auto d = make("12.34");
    EXPECT_EQ(d.to_string(), "12.34");
}

TEST(Decimal, WhenParsingNegative_StoresCorrectly) {
    auto d = make("-100.5");
    EXPECT_TRUE(d.is_negative());
    EXPECT_EQ(d.to_string(), "-100.5");
}

TEST(Decimal, WhenParsingSmallFraction_PreservesPrecision) {
    auto d = make("0.0001");
    EXPECT_EQ(d.to_string(), "0.0001");
}

TEST(Decimal, WhenParsingTenDecimalPlaces_PreservesAll) {
    auto d = make("1.0123456789");
    EXPECT_EQ(d.to_string(), "1.0123456789");
}

TEST(Decimal, WhenParsingZero_IsZero) {
    auto d = make("0");
    EXPECT_TRUE(d.is_zero());
    EXPECT_EQ(d.to_string(), "0");
}

TEST(Decimal, WhenParsingWithLeadingPlus_Accepts) {
    auto d = make("+5.25");
    EXPECT_EQ(d.to_string(), "5.25");
}

TEST(Decimal, WhenParsingWithWhitespace_Trims) {
    auto d = make("  7.5  ");
    EXPECT_EQ(d.to_string(), "7.5");
}

TEST(Decimal, WhenParsingLargeAmount_WithinNumeric20_8) {
    // NUMERIC(20,8) max ~ 999,999,999,999.99999999
    auto d = make("999999999999.99999999");
    EXPECT_EQ(d.to_string(), "999999999999.99999999");
}

// ---- Parsing: error paths ----

TEST(Decimal, WhenParsingEmpty_ReturnsError) {
    auto result = Decimal::parse("");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::ParseError);
}

TEST(Decimal, WhenParsingInvalidChar_ReturnsError) {
    auto result = Decimal::parse("12.3a");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::ParseError);
}

TEST(Decimal, WhenParsingMultipleDots_ReturnsError) {
    auto result = Decimal::parse("1.2.3");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::ParseError);
}

TEST(Decimal, WhenParsingOnlySign_ReturnsError) {
    auto result = Decimal::parse("-");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::ParseError);
}

// ---- Half-Even rounding on parse ----

TEST(Decimal, WhenParsingBeyondScaleRoundHalfToEven_RoundsDown) {
    // 11th+ digits: ...5 with even digit before -> round down (to even)
    // 0.00000000005 -> scale 10 keeps 0.0000000000, halfway, last kept digit 0 (even)
    auto d = make("0.00000000005");
    EXPECT_EQ(d.to_string(), "0");
}

TEST(Decimal, WhenParsingBeyondScaleRoundHalfToEven_RoundsUp) {
    // 0.00000000015 -> halfway, last kept digit 1 (odd) -> round up to 0.0000000002
    auto d = make("0.00000000015");
    EXPECT_EQ(d.to_string(), "0.0000000002");
}

TEST(Decimal, WhenParsingBeyondScaleAboveHalf_RoundsUp) {
    // 0.00000000016 -> above half -> round up
    auto d = make("0.00000000016");
    EXPECT_EQ(d.to_string(), "0.0000000002");
}

TEST(Decimal, WhenParsingBeyondScaleBelowHalf_RoundsDown) {
    // 0.00000000014 -> below half -> round down
    auto d = make("0.00000000014");
    EXPECT_EQ(d.to_string(), "0.0000000001");
}

// ---- Arithmetic: addition / subtraction ----

TEST(Decimal, WhenAdding_ReturnsSum) {
    auto a = make("100.50");
    auto b = make("50.25");
    auto result = a.add(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "150.75");
}

TEST(Decimal, WhenAddingNegative_ReturnsCorrectResult) {
    auto a = make("100");
    auto b = make("-30");
    auto result = a.add(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "70");
}

TEST(Decimal, WhenSubtracting_ReturnsDifference) {
    auto a = make("100.75");
    auto b = make("0.25");
    auto result = a.subtract(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "100.5");
}

TEST(Decimal, WhenSubtractingToNegative_ReturnsNegative) {
    auto a = make("10");
    auto b = make("25");
    auto result = a.subtract(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "-15");
}

// ---- Arithmetic: the classic float trap ----

TEST(Decimal, WhenAddingPointOnePlusPointTwo_ExactlyPointThree) {
    // 0.1 + 0.2 must equal exactly 0.3 (impossible with binary float)
    auto a = make("0.1");
    auto b = make("0.2");
    auto result = a.add(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "0.3");
}

// ---- Arithmetic: multiplication ----

TEST(Decimal, WhenMultiplying_ReturnsProduct) {
    auto a = make("12.5");
    auto b = make("4");
    auto result = a.multiply(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "50");
}

TEST(Decimal, WhenMultiplyingByExchangeRate_PreservesValue) {
    // 1000 USD * 7.18 = 7180
    auto amount = make("1000");
    auto rate = make("7.18");
    auto result = amount.multiply(rate);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "7180");
}

TEST(Decimal, WhenMultiplyingNegatives_ReturnsPositive) {
    auto a = make("-3");
    auto b = make("-4");
    auto result = a.multiply(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "12");
}

TEST(Decimal, WhenMultiplyingByZero_ReturnsZero) {
    auto a = make("999.99");
    auto b = make("0");
    auto result = a.multiply(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_zero());
}

// ---- Arithmetic: division ----

TEST(Decimal, WhenDividing_ReturnsQuotient) {
    auto a = make("100");
    auto b = make("4");
    auto result = a.divide(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "25");
}

TEST(Decimal, WhenDividingWithRepeatingDecimal_RoundsHalfEven) {
    // 1 / 3 = 0.3333333333 (10 digits, Half-Even)
    auto a = make("1");
    auto b = make("3");
    auto result = a.divide(b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "0.3333333333");
}

TEST(Decimal, WhenDividingByZero_ReturnsError) {
    auto a = make("100");
    auto b = make("0");
    auto result = a.divide(b);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::DivisionByZero);
}

TEST(Decimal, WhenDeriveRateFromAmounts_ReturnsCorrectRate) {
    // Transfer mode B: 7170 CNY / 1000 USD => 7.17
    auto incoming = make("7170");
    auto outgoing = make("1000");
    auto rate = incoming.divide(outgoing);
    ASSERT_TRUE(rate.has_value());
    EXPECT_EQ(rate->to_string(), "7.17");
}

// ---- Comparison ----

TEST(Decimal, WhenComparing_OrdersCorrectly) {
    auto a = make("10.5");
    auto b = make("10.6");
    EXPECT_LT(a, b);
    EXPECT_GT(b, a);
    EXPECT_NE(a, b);
}

TEST(Decimal, WhenEqualValues_AreEqual) {
    auto a = make("42.00");
    auto b = make("42");
    EXPECT_EQ(a, b);
}

TEST(Decimal, WhenComparingNegatives_OrdersCorrectly) {
    auto a = make("-10");
    auto b = make("-5");
    EXPECT_LT(a, b);
}

// ---- Unary operations ----

TEST(Decimal, WhenNegated_FlipsSign) {
    auto a = make("42.5");
    auto neg = a.negated();
    EXPECT_EQ(neg.to_string(), "-42.5");
    EXPECT_EQ(neg.negated().to_string(), "42.5");
}

TEST(Decimal, WhenAbs_ReturnsMagnitude) {
    auto a = make("-42.5");
    EXPECT_EQ(a.abs().to_string(), "42.5");
    auto b = make("42.5");
    EXPECT_EQ(b.abs().to_string(), "42.5");
}

TEST(Decimal, WhenFromInteger_StoresCorrectly) {
    auto result = Decimal::from_integer(12345);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->to_string(), "12345");
}

// ---- Predicates ----

TEST(Decimal, WhenZero_PredicatesCorrect) {
    auto d = make("0");
    EXPECT_TRUE(d.is_zero());
    EXPECT_FALSE(d.is_negative());
    EXPECT_FALSE(d.is_positive());
}

TEST(Decimal, WhenPositive_PredicatesCorrect) {
    auto d = make("1");
    EXPECT_FALSE(d.is_zero());
    EXPECT_FALSE(d.is_negative());
    EXPECT_TRUE(d.is_positive());
}

// ---- from_scaled / raw_value round-trip ----

TEST(Decimal, WhenFromScaled_RawValueRoundTrips) {
    // 12.34 at scale 10 -> raw 123400000000
    auto d = make("12.34");
    auto rebuilt = Decimal::from_scaled(d.raw_value());
    EXPECT_EQ(rebuilt.to_string(), "12.34");
    EXPECT_EQ(rebuilt, d);
}

TEST(Decimal, WhenRawValueOfOne_IsScaleFactor) {
    auto one = make("1");
    EXPECT_EQ(one.raw_value(), Decimal::kScaleFactor);
}

// ---- multiply range ----

TEST(Decimal, WhenMultiplyWithinRange_Succeeds) {
    // 1e9 * 1e9 = 1e18. Each operand carries the 1e10 scale, so the
    // intermediate product is (1e9*1e10)^2 = 1e38, near but under __int128 max.
    auto a = make("1000000000");
    auto b = make("1000000000");
    auto r = a.multiply(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->to_string(), "1000000000000000000");
}

TEST(Decimal, WhenMultiplyIntermediateOverflows_ReturnsError) {
    // Known limitation: the intermediate product carries scale^2, so two large
    // operands overflow __int128 even when the true result (1e22) would fit.
    // 1e11 * 1e11: (1e11*1e10)^2 = 1e42 > __int128 max (~1.7e38) -> overflow.
    auto a = make("100000000000");
    auto b = make("100000000000");
    auto r = a.multiply(b);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, DomainErrorCode::Overflow);
}

} // namespace pfh::test
