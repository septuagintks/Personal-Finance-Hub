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
    // The user's incoming amount is authoritative and MUST NOT be modified.
    // outgoing = 7180 / 7.18 = 1000 USD (exact); incoming stays exactly 7180 CNY.
    EXPECT_EQ(transfer->incoming().amount().to_string(), "7180 CNY");
    EXPECT_EQ(transfer->outgoing().amount().to_string(), "1000 USD");
    EXPECT_TRUE(transfer->rate().has_value());
    EXPECT_EQ(transfer->mode(), TransferMode::IncomingAndRate);
}

TEST(TransferDomainService, WhenBuildFromIncomingAndRate_PreservesUserIncomingUnderRounding) {
    // Non-exact division: 7170 / 7.18 rounds on the derived outgoing side, but
    // the user's incoming amount is preserved verbatim (never rewritten).
    auto incoming = money("7170", "CNY");
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

    ASSERT_TRUE(transfer.has_value()) << transfer.error().message;
    EXPECT_EQ(transfer->incoming().amount().to_string(), "7170 CNY");
    EXPECT_EQ(transfer->outgoing().amount().to_string(), "998.60724234 USD");
    EXPECT_EQ(transfer->mode(), TransferMode::IncomingAndRate);
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

// ---- Fee adjustments ----

TEST(TransferDomainService, WhenSourceFeeProvided_CreatesNegativeAdjustment) {
    auto transfer = TransferDomainService::build_from_both_amounts(
        money("100", "USD"),
        money("100", "USD"),
        AccountId(1),
        AccountId(2),
        UserId(100),
        sample_time(),
        "Internal move",
        TransferGroupId(999),
        TransferFee{FeeSource::SourceAccount, AccountId(1), money("2.5", "USD")});

    ASSERT_TRUE(transfer.has_value()) << transfer.error().message;
    ASSERT_EQ(transfer->adjustments().size(), 1u);
    const auto& fee = transfer->adjustments().front();
    EXPECT_EQ(fee.type(), TransactionType::Adjustment);
    EXPECT_EQ(fee.account_id(), AccountId(1));
    EXPECT_EQ(fee.user_id(), UserId(100));
    EXPECT_EQ(fee.amount().to_string(), "-2.5 USD");
    EXPECT_EQ(fee.transfer_group_id(), transfer->transfer_group_id());
    EXPECT_EQ(fee.occurred_at(), sample_time());
    EXPECT_EQ(fee.description(), "Transfer fee: Internal move");
}

TEST(TransferDomainService, WhenTargetFeeProvided_UsesTargetCurrencyAndAccount) {
    auto transfer = TransferDomainService::build_from_outgoing_and_rate(
        money("100", "USD"),
        AccountId(1),
        AccountId(2),
        rate("USD", "CNY", "7.18"),
        UserId(100),
        sample_time(),
        "Cross-currency move",
        TransferGroupId(999),
        TransferFee{FeeSource::TargetAccount, AccountId(2), money("3", "CNY")});

    ASSERT_TRUE(transfer.has_value()) << transfer.error().message;
    ASSERT_EQ(transfer->adjustments().size(), 1u);
    EXPECT_EQ(transfer->adjustments().front().account_id(), AccountId(2));
    EXPECT_EQ(transfer->adjustments().front().amount().to_string(), "-3 CNY");
}

TEST(TransferDomainService, WhenThirdPartyFeeProvided_AllowsItsOwnCurrency) {
    auto transfer = TransferDomainService::build_from_both_amounts(
        money("100", "USD"),
        money("100", "USD"),
        AccountId(1),
        AccountId(2),
        UserId(100),
        sample_time(),
        "Internal move",
        TransferGroupId(999),
        TransferFee{FeeSource::ThirdParty, AccountId(3), money("5", "CNY")});

    ASSERT_TRUE(transfer.has_value()) << transfer.error().message;
    ASSERT_EQ(transfer->adjustments().size(), 1u);
    EXPECT_EQ(transfer->adjustments().front().account_id(), AccountId(3));
    EXPECT_EQ(transfer->adjustments().front().amount().to_string(), "-5 CNY");
}

TEST(TransferDomainService, WhenFeeIsNotPositive_ReturnsError) {
    for (const auto* invalid_amount : {"0", "-1"}) {
        auto transfer = TransferDomainService::build_from_both_amounts(
            money("100", "USD"),
            money("100", "USD"),
            AccountId(1),
            AccountId(2),
            UserId(100),
            sample_time(),
            "Internal move",
            TransferGroupId(999),
            TransferFee{
                FeeSource::SourceAccount,
                AccountId(1),
                money(invalid_amount, "USD")});

        ASSERT_FALSE(transfer.has_value());
        EXPECT_EQ(transfer.error().code, DomainErrorCode::InvalidAmount);
    }
}

TEST(TransferDomainService, WhenFeeSourceAccountDoesNotMatch_ReturnsError) {
    auto transfer = TransferDomainService::build_from_both_amounts(
        money("100", "USD"),
        money("100", "USD"),
        AccountId(1),
        AccountId(2),
        UserId(100),
        sample_time(),
        "Internal move",
        TransferGroupId(999),
        TransferFee{FeeSource::SourceAccount, AccountId(2), money("2", "USD")});

    ASSERT_FALSE(transfer.has_value());
    EXPECT_EQ(transfer.error().code, DomainErrorCode::InvalidOperation);
}

TEST(TransferDomainService, WhenFeeCurrencyDoesNotMatchSelectedLeg_ReturnsError) {
    auto transfer = TransferDomainService::build_from_outgoing_and_rate(
        money("100", "USD"),
        AccountId(1),
        AccountId(2),
        rate("USD", "CNY", "7.18"),
        UserId(100),
        sample_time(),
        "Cross-currency move",
        TransferGroupId(999),
        TransferFee{FeeSource::TargetAccount, AccountId(2), money("3", "USD")});

    ASSERT_FALSE(transfer.has_value());
    EXPECT_EQ(transfer.error().code, DomainErrorCode::CurrencyMismatch);
}

} // namespace pfh::test
