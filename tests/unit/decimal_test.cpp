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

Decimal from_raw(Decimal::StorageType raw) {
    auto result = Decimal::from_scaled(raw);
    EXPECT_TRUE(result.has_value());
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
    ASSERT_TRUE(rebuilt.has_value());
    EXPECT_EQ(rebuilt->to_string(), "12.34");
    EXPECT_EQ(*rebuilt, d);
}

TEST(Decimal, WhenFromScaledAtSafeBounds_AcceptsBothSigns) {
    auto maximum = Decimal::from_scaled(Decimal::kMaxRawValue);
    auto minimum = Decimal::from_scaled(Decimal::kMinRawValue);

    ASSERT_TRUE(maximum.has_value());
    ASSERT_TRUE(minimum.has_value());
    EXPECT_TRUE(maximum->raw_value() == Decimal::kMaxRawValue);
    EXPECT_TRUE(minimum->raw_value() == Decimal::kMinRawValue);
}

TEST(Decimal, WhenFromScaledOutsideSafeBounds_ReturnsOverflow) {
    auto above = Decimal::from_scaled(Decimal::kMaxRawValue + 1);
    auto below = Decimal::from_scaled(Decimal::kMinRawValue - 1);

    ASSERT_FALSE(above.has_value());
    ASSERT_FALSE(below.has_value());
    EXPECT_EQ(above.error().code, DomainErrorCode::Overflow);
    EXPECT_EQ(below.error().code, DomainErrorCode::Overflow);
}

TEST(Decimal, WhenNegatingAndTakingAbsAtBounds_RemainsDefined) {
    auto maximum = from_raw(Decimal::kMaxRawValue);
    auto minimum = from_raw(Decimal::kMinRawValue);

    EXPECT_TRUE(maximum.negated().raw_value() == Decimal::kMinRawValue);
    EXPECT_TRUE(minimum.negated().raw_value() == Decimal::kMaxRawValue);
    EXPECT_TRUE(minimum.abs().raw_value() == Decimal::kMaxRawValue);
}

TEST(Decimal, WhenSafeBoundsRoundTripThroughString_PreserveRawValues) {
    auto maximum = from_raw(Decimal::kMaxRawValue);
    auto minimum = from_raw(Decimal::kMinRawValue);

    auto reparsed_maximum = Decimal::parse(maximum.to_string());
    auto reparsed_minimum = Decimal::parse(minimum.to_string());

    ASSERT_TRUE(reparsed_maximum.has_value());
    ASSERT_TRUE(reparsed_minimum.has_value());
    EXPECT_EQ(*reparsed_maximum, maximum);
    EXPECT_EQ(*reparsed_minimum, minimum);
}

TEST(Decimal, WhenAddingOrSubtractingPastSafeBounds_ReturnsOverflow) {
    auto maximum = from_raw(Decimal::kMaxRawValue);
    auto minimum = from_raw(Decimal::kMinRawValue);
    auto one_raw = from_raw(1);

    auto positive_overflow = maximum.add(one_raw);
    auto negative_overflow = minimum.subtract(one_raw);
    auto doubled_minimum = minimum.add(minimum);

    ASSERT_FALSE(positive_overflow.has_value());
    ASSERT_FALSE(negative_overflow.has_value());
    ASSERT_FALSE(doubled_minimum.has_value());
    EXPECT_EQ(positive_overflow.error().code, DomainErrorCode::Overflow);
    EXPECT_EQ(negative_overflow.error().code, DomainErrorCode::Overflow);
    EXPECT_EQ(doubled_minimum.error().code, DomainErrorCode::Overflow);
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

TEST(Decimal, WhenMultiplyIntermediateExceeds128Bits_ComputesRepresentableResult) {
    // The unscaled intermediate needs more than 128 bits, but decomposition
    // around the fixed scale keeps the representable final result exact.
    auto a = make("100000000000");
    auto b = make("100000000000");
    auto r = a.multiply(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->to_string(), "10000000000000000000000");
}

TEST(Decimal, WhenMultiplyingAndDividingSafeBoundsByOne_PreservesValue) {
    auto maximum = from_raw(Decimal::kMaxRawValue);
    auto minimum = from_raw(Decimal::kMinRawValue);
    auto one = make("1");

    auto positive_product = maximum.multiply(one);
    auto negative_product = minimum.multiply(one);
    auto positive_quotient = maximum.divide(one);
    auto negative_quotient = minimum.divide(one);

    ASSERT_TRUE(positive_product.has_value());
    ASSERT_TRUE(negative_product.has_value());
    ASSERT_TRUE(positive_quotient.has_value());
    ASSERT_TRUE(negative_quotient.has_value());
    EXPECT_EQ(*positive_product, maximum);
    EXPECT_EQ(*negative_product, minimum);
    EXPECT_EQ(*positive_quotient, maximum);
    EXPECT_EQ(*negative_quotient, minimum);
}

TEST(Decimal, WhenMultiplicationIsHalfway_RoundsToEvenSymmetrically) {
    auto half_of_even = make("0.0000000001").multiply(make("0.5"));
    auto half_of_odd = make("0.0000000003").multiply(make("0.5"));
    auto negative_half_of_odd = make("-0.0000000003").multiply(make("0.5"));

    ASSERT_TRUE(half_of_even.has_value());
    ASSERT_TRUE(half_of_odd.has_value());
    ASSERT_TRUE(negative_half_of_odd.has_value());
    EXPECT_EQ(half_of_even->to_string(), "0");
    EXPECT_EQ(half_of_odd->to_string(), "0.0000000002");
    EXPECT_EQ(negative_half_of_odd->to_string(), "-0.0000000002");
}

TEST(Decimal, WhenDivisionIsHalfway_RoundsToEvenSymmetrically) {
    auto half_of_even = make("0.0000000001").divide(make("2"));
    auto half_of_odd = make("0.0000000003").divide(make("2"));
    auto negative_half_of_odd = make("-0.0000000003").divide(make("2"));

    ASSERT_TRUE(half_of_even.has_value());
    ASSERT_TRUE(half_of_odd.has_value());
    ASSERT_TRUE(negative_half_of_odd.has_value());
    EXPECT_EQ(half_of_even->to_string(), "0");
    EXPECT_EQ(half_of_odd->to_string(), "0.0000000002");
    EXPECT_EQ(negative_half_of_odd->to_string(), "-0.0000000002");
}

// ---- Item 10: DB NUMERIC(20,8) / NUMERIC(20,10) boundary ----

TEST(Decimal, FitsNumeric208_AcceptsInRangeValue) {
    // 12 integer digits + 8 fractional digits is the maximum for NUMERIC(20,8).
    EXPECT_TRUE(make("999999999999.99999999").fits_numeric_20_8());
    EXPECT_TRUE(make("0").fits_numeric_20_8());
    EXPECT_TRUE(make("-100.5").fits_numeric_20_8());
    EXPECT_TRUE(make("12345.678").fits_numeric_20_8());
}

TEST(Decimal, FitsNumeric208_RejectsTooManyFractionalDigits) {
    // 9 fractional digits exceeds scale 8 (would round on write).
    EXPECT_FALSE(make("0.000000001").fits_numeric_20_8());
    EXPECT_FALSE(make("1.123456789").fits_numeric_20_8());
}

TEST(Decimal, FitsNumeric208_RejectsIntegerPartTooLarge) {
    // 13 integer digits exceeds precision-scale = 12.
    EXPECT_FALSE(make("1000000000000").fits_numeric_20_8());
    EXPECT_FALSE(make("-1000000000000.5").fits_numeric_20_8());
}

TEST(Decimal, FitsNumeric2010_AllowsTenFractionalDigits) {
    // Rate column keeps 10 fractional digits.
    EXPECT_TRUE(make("7.1800000001").fits_numeric_20_10());
    EXPECT_FALSE(make("0.000000001").fits_numeric_20_8()); // but not for amount
}

TEST(Decimal, ParseNumeric208_RejectsPrecisionBeforeInternalRounding) {
    auto hidden_precision = Decimal::parse_numeric_20_8("1.00000000001");
    ASSERT_FALSE(hidden_precision.has_value());
    EXPECT_EQ(hidden_precision.error().code, DomainErrorCode::InvalidAmount);

    auto trailing_zeros = Decimal::parse_numeric_20_8("1.25000000000");
    ASSERT_TRUE(trailing_zeros.has_value());
    EXPECT_EQ(trailing_zeros->to_string(), "1.25");

    auto hidden_rate_precision =
        Decimal::parse_numeric_20_10("1.00000000001");
    ASSERT_FALSE(hidden_rate_precision.has_value());
    EXPECT_EQ(hidden_rate_precision.error().code,
              DomainErrorCode::InvalidExchangeRate);
}

TEST(Decimal, RoundToScale_UsesHalfEven) {
    auto down = make("1.234567845").round_to_scale(8);
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(down->to_string(), "1.23456784");

    auto up = make("1.234567855").round_to_scale(8);
    ASSERT_TRUE(up.has_value());
    EXPECT_EQ(up->to_string(), "1.23456786");

    auto negative_down = make("-1.234567845").round_to_scale(8);
    ASSERT_TRUE(negative_down.has_value());
    EXPECT_EQ(negative_down->to_string(), "-1.23456784");

    auto negative_up = make("-1.234567855").round_to_scale(8);
    ASSERT_TRUE(negative_up.has_value());
    EXPECT_EQ(negative_up->to_string(), "-1.23456786");
}

} // namespace pfh::test
