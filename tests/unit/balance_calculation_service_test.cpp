// Personal Finance Hub - BalanceCalculationService Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/balance_calculation_service.h"
#include "test_support.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

// ---- Basic balance calculation ----

TEST(BalanceCalculationService, WhenNoTransactions_BalanceIsZero) {
    std::vector<Transaction> txs;
    auto balance = BalanceCalculationService::calculate_balance(
        AccountId(1),
        txs,
        ccy("USD"),
        sample_time());

    ASSERT_TRUE(balance.has_value());
    EXPECT_EQ(balance->balance.to_string(), "0 USD");
}

TEST(BalanceCalculationService, WhenIncomeTransactions_AddsToBalance) {
    std::vector<Transaction> txs;
    txs.push_back(Transaction(
        TransactionId(1),
        UserId(100),
        AccountId(1),
        money("1000", "USD"),
        TransactionType::Income,
        sample_time(),
        "Salary"));
    txs.push_back(Transaction(
        TransactionId(2),
        UserId(100),
        AccountId(1),
        money("200", "USD"),
        TransactionType::Income,
        sample_time(),
        "Bonus"));

    auto balance = BalanceCalculationService::calculate_balance(
        AccountId(1),
        txs,
        ccy("USD"),
        sample_time());

    ASSERT_TRUE(balance.has_value());
    EXPECT_EQ(balance->balance.to_string(), "1200 USD");
}

TEST(BalanceCalculationService, WhenExpenseTransactions_SubtractsFromBalance) {
    std::vector<Transaction> txs;
    txs.push_back(Transaction(
        TransactionId(1),
        UserId(100),
        AccountId(1),
        money("1000", "USD"),
        TransactionType::Income,
        sample_time(),
        "Salary"));
    txs.push_back(Transaction(
        TransactionId(2),
        UserId(100),
        AccountId(1),
        money("300", "USD"),
        TransactionType::Expense,
        sample_time(),
        "Rent"));

    auto balance = BalanceCalculationService::calculate_balance(
        AccountId(1),
        txs,
        ccy("USD"),
        sample_time());

    ASSERT_TRUE(balance.has_value());
    EXPECT_EQ(balance->balance.to_string(), "700 USD");
}

TEST(BalanceCalculationService, WhenMixedTransactions_ComputesCorrectBalance) {
    std::vector<Transaction> txs;
    txs.push_back(Transaction(
        TransactionId(1),
        UserId(100),
        AccountId(1),
        money("1000", "USD"),
        TransactionType::Income,
        sample_time(),
        "Salary"));
    txs.push_back(Transaction(
        TransactionId(2),
        UserId(100),
        AccountId(1),
        money("200", "USD"),
        TransactionType::Expense,
        sample_time(),
        "Groceries"));
    txs.push_back(Transaction(
        TransactionId(3),
        UserId(100),
        AccountId(1),
        money("50", "USD"),
        TransactionType::Expense,
        sample_time(),
        "Gas"));
    txs.push_back(Transaction(
        TransactionId(4),
        UserId(100),
        AccountId(1),
        money("100", "USD"),
        TransactionType::Income,
        sample_time(),
        "Refund"));

    auto balance = BalanceCalculationService::calculate_balance(
        AccountId(1),
        txs,
        ccy("USD"),
        sample_time());

    ASSERT_TRUE(balance.has_value());
    EXPECT_EQ(balance->balance.to_string(), "850 USD");
}

// ---- Soft deletion ----

TEST(BalanceCalculationService, WhenTransactionDeleted_ExcludesFromBalance) {
    std::vector<Transaction> txs;
    txs.push_back(Transaction(
        TransactionId(1),
        UserId(100),
        AccountId(1),
        money("1000", "USD"),
        TransactionType::Income,
        sample_time(),
        "Salary"));

    Transaction deleted_tx(
        TransactionId(2),
        UserId(100),
        AccountId(1),
        money("200", "USD"),
        TransactionType::Expense,
        sample_time(),
        "Canceled purchase");
    deleted_tx.mark_deleted(sample_time());
    txs.push_back(deleted_tx);

    auto balance = BalanceCalculationService::calculate_balance(
        AccountId(1),
        txs,
        ccy("USD"),
        sample_time());

    ASSERT_TRUE(balance.has_value());
    EXPECT_EQ(balance->balance.to_string(), "1000 USD");
}

// ---- Historical balance ----

TEST(BalanceCalculationService, WhenCalculatingHistoricalBalance_ExcludesFutureTransactions) {
    auto t1 = std::chrono::system_clock::now();
    auto t2 = t1 + std::chrono::hours(24);
    auto t3 = t1 + std::chrono::hours(48);
    auto cutoff = t1 + std::chrono::hours(36);

    std::vector<Transaction> txs;
    txs.push_back(Transaction(
        TransactionId(1),
        UserId(100),
        AccountId(1),
        money("1000", "USD"),
        TransactionType::Income,
        t1,
        "Day 1"));
    txs.push_back(Transaction(
        TransactionId(2),
        UserId(100),
        AccountId(1),
        money("200", "USD"),
        TransactionType::Expense,
        t2,
        "Day 2"));
    txs.push_back(Transaction(
        TransactionId(3),
        UserId(100),
        AccountId(1),
        money("300", "USD"),
        TransactionType::Expense,
        t3,
        "Day 3"));

    auto balance = BalanceCalculationService::calculate_balance_at(
        AccountId(1),
        txs,
        ccy("USD"),
        cutoff);

    ASSERT_TRUE(balance.has_value());
    // Should include t1 (1000) and t2 (-200), but not t3 (-300)
    EXPECT_EQ(balance->balance.to_string(), "800 USD");
}

// ---- Currency mismatch ----

TEST(BalanceCalculationService, WhenTransactionCurrencyMismatch_ReturnsError) {
    std::vector<Transaction> txs;
    txs.push_back(Transaction(
        TransactionId(1),
        UserId(100),
        AccountId(1),
        money("1000", "CNY"),  // CNY, but account is USD
        TransactionType::Income,
        sample_time(),
        "Mismatched currency"));

    auto balance = BalanceCalculationService::calculate_balance(
        AccountId(1),
        txs,
        ccy("USD"),
        sample_time());

    ASSERT_FALSE(balance.has_value());
    EXPECT_EQ(balance.error().code, DomainErrorCode::CurrencyMismatch);
}

} // namespace pfh::test
