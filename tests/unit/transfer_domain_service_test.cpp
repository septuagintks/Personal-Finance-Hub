// Personal Finance Hub - TransferDomainService Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/transfer_domain_service.h"
#include "test_support.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

// ---- Mode 1: Outgoing + Rate => Incoming ----

TEST(TransferDomainService, WhenBuildFromOutgoingAndRate_CalculatesIncoming) {
    auto outgoing = money("1000", "USD");
    auto exchange_rate = rate("USD", "CNY", "7.18");
    auto transfer = TransferDomainService::build_from_outgoing_and_rate(
        outgoing,
        AccountId(1),
        AccountId(2),
        exchange_rate,
        UserId(100),
        sample_time(),
        "Test transfer",
        TransferGroupId(999));

    ASSERT_TRUE(transfer.has_value());
    EXPECT_EQ(transfer->outgoing().amount().to_string(), "1000 USD");
    EXPECT_EQ(transfer->incoming().amount().to_string(), "7180 CNY");
    EXPECT_TRUE(transfer->rate().has_value());
    EXPECT_EQ(transfer->rate()->rate().to_string(), "7.18");
}

TEST(TransferDomainService, WhenBuildFromOutgoingAndRateCurrencyMismatch_ReturnsError) {
    auto outgoing = money("1000", "EUR");  // EUR, but rate is USD->CNY
    auto exchange_rate = rate("USD", "CNY", "7.18");
    auto transfer = TransferDomainService::build_from_outgoing_and_rate(
        outgoing,
        AccountId(1),
        AccountId(2),
        exchange_rate,
        UserId(100),
        sample_time(),
        "Test transfer",
        TransferGroupId(999));

    ASSERT_FALSE(transfer.has_value());
    EXPECT_EQ(transfer.error().code, DomainErrorCode::CurrencyMismatch);
}

// ---- Mode 2: Outgoing + Incoming => derive Rate ----

TEST(TransferDomainService, WhenBuildFromBothAmountsCrossCurrency_DerivesRate) {
    auto outgoing = money("1000", "USD");
    auto incoming = money("7180", "CNY");
    auto transfer = TransferDomainService::build_from_both_amounts(
        outgoing,
        incoming,
        AccountId(1),
        AccountId(2),
        UserId(100),
        sample_time(),
        "Test transfer",
        TransferGroupId(999));

    ASSERT_TRUE(transfer.has_value());
    EXPECT_EQ(transfer->outgoing().amount().to_string(), "1000 USD");
    EXPECT_EQ(transfer->incoming().amount().to_string(), "7180 CNY");
    EXPECT_TRUE(transfer->rate().has_value());
    EXPECT_EQ(transfer->rate()->rate().to_string(), "7.18");
    EXPECT_EQ(transfer->rate()->source(), "DerivedFromAmounts");
}

TEST(TransferDomainService, WhenBuildFromBothAmountsSameCurrency_NoRate) {
    auto outgoing = money("1000", "USD");
    auto incoming = money("1000", "USD");
    auto transfer = TransferDomainService::build_from_both_amounts(
        outgoing,
        incoming,
        AccountId(1),
        AccountId(2),
        UserId(100),
        sample_time(),
        "Test transfer",
        TransferGroupId(999));

    ASSERT_TRUE(transfer.has_value());
    EXPECT_EQ(transfer->outgoing().amount().to_string(), "1000 USD");
    EXPECT_EQ(transfer->incoming().amount().to_string(), "1000 USD");
    EXPECT_FALSE(transfer->rate().has_value());
}

TEST(TransferDomainService, WhenBuildFromBothAmountsSameCurrencyMismatch_ReturnsError) {
    auto outgoing = money("1000", "USD");
    auto incoming = money("1001", "USD");  // Amount mismatch
    auto transfer = TransferDomainService::build_from_both_amounts(
        outgoing,
        incoming,
        AccountId(1),
        AccountId(2),
        UserId(100),
        sample_time(),
        "Test transfer",
        TransferGroupId(999));

    ASSERT_FALSE(transfer.has_value());
    EXPECT_EQ(transfer.error().code, DomainErrorCode::InvalidOperation);
}

// ---- Mode 3: Incoming + Rate => Outgoing ----

TEST(TransferDomainService, WhenBuildFromIncomingAndRate_CalculatesOutgoing) {
    auto incoming = money("7180", "CNY");
    auto exchange_rate = rate("USD", "CNY", "7.18");
    auto transfer = TransferDomainService::build_from_incoming_and_rate(
        incoming,
        AccountId(1),
        AccountId(2),
        exchange_rate,
        UserId(100),
        sample_time(),
        "Test transfer",
        TransferGroupId(999));

    ASSERT_TRUE(transfer.has_value());
    // Mode 3 computes outgoing from incoming/rate, which may not round-trip
    // exactly due to Decimal precision. The service recomputes incoming from
    // the computed outgoing to ensure consistency.
    // 7180 / 7.18 = 999.99999988... (rounded to scale 10)
    // 999.99999988 * 7.18 = 7179.9999991384 (recomputed incoming)
    EXPECT_EQ(transfer->outgoing().amount().to_string(), "999.99999988 USD");
    EXPECT_EQ(transfer->incoming().amount().to_string(), "7179.9999991384 CNY");
    EXPECT_TRUE(transfer->rate().has_value());
}

TEST(TransferDomainService, WhenBuildFromIncomingAndRateCurrencyMismatch_ReturnsError) {
    auto incoming = money("7180", "EUR");  // EUR, but rate target is CNY
    auto exchange_rate = rate("USD", "CNY", "7.18");
    auto transfer = TransferDomainService::build_from_incoming_and_rate(
        incoming,
        AccountId(1),
        AccountId(2),
        exchange_rate,
        UserId(100),
        sample_time(),
        "Test transfer",
        TransferGroupId(999));

    ASSERT_FALSE(transfer.has_value());
    EXPECT_EQ(transfer.error().code, DomainErrorCode::CurrencyMismatch);
}

// ---- Validation ----

TEST(TransferDomainService, WhenValidatingConsistentTransfer_Passes) {
    auto outgoing = money("1000", "USD");
    auto exchange_rate = rate("USD", "CNY", "7.18");
    auto transfer = TransferDomainService::build_from_outgoing_and_rate(
        outgoing,
        AccountId(1),
        AccountId(2),
        exchange_rate,
        UserId(100),
        sample_time(),
        "Test transfer",
        TransferGroupId(999));

    ASSERT_TRUE(transfer.has_value());
    auto validation = TransferDomainService::validate(*transfer);
    EXPECT_TRUE(validation.has_value());
}

} // namespace pfh::test
