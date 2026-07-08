// Personal Finance Hub - BalanceCalculationService
// Version: 1.0
// C++23
//
// BalanceCalculationService is a pure domain service that reconstructs account
// balances from a collection of Transactions. It does NOT access repositories
// or open transactions; it operates purely on in-memory data passed to it.
//
// Key rules:
// 1. Exclude soft-deleted transactions (deleted_at is set).
// 2. Income adds to balance; Expense subtracts.
// 3. Transfer: outgoing subtracts, incoming adds (both already marked Transfer).
// 4. Adjustment can be positive or negative (context-dependent).

#pragma once

#include "pfh/domain/domain_error.h"
#include "pfh/domain/money.h"
#include "pfh/domain/transaction.h"
#include <vector>

namespace pfh::domain {

/// @brief Balance snapshot for an account at a point in time.
///
/// This is a value object used as the result of balance calculation. It does
/// NOT represent a first-class entity; it's a read model.
struct BalanceSnapshot {
    AccountId account_id;
    Money balance;
    Transaction::TimePoint as_of;

    BalanceSnapshot(AccountId acc, Money bal, Transaction::TimePoint t)
        : account_id(acc), balance(std::move(bal)), as_of(t) {}
};

/// @brief Pure domain service for balance calculation.
///
/// This service does NOT:
/// - Call repositories
/// - Open database transactions
/// - Publish domain events
///
/// It operates on a provided collection of transactions and returns the
/// computed balance.
class BalanceCalculationService {
public:
    /// @brief Calculate the balance of an account from its transaction history.
    ///
    /// Rules:
    /// - Transactions with deleted_at set are excluded.
    /// - Income: +amount
    /// - Expense: -amount
    /// - Transfer (outgoing): -amount
    /// - Transfer (incoming): +amount
    /// - Adjustment: can be + or - depending on the adjustment nature; here we
    ///   treat it as signed (positive adjustment adds, but typically adjustments
    ///   like fees would be stored as Expense or negative Adjustment).
    ///
    /// @param account_id The account to calculate balance for.
    /// @param transactions All transactions for this account (filtered by caller).
    /// @param currency The account's currency (for validation).
    /// @param as_of The timestamp at which to compute the balance (defaults to now).
    /// @return BalanceSnapshot on success, DomainError if currency mismatch occurs.
    [[nodiscard]] static DomainResult<BalanceSnapshot> calculate_balance(
        AccountId account_id,
        const std::vector<Transaction>& transactions,
        const Currency& currency,
        Transaction::TimePoint as_of = std::chrono::system_clock::now());

    /// @brief Calculate balance considering only transactions up to a given time.
    ///
    /// This is useful for historical balance reconstruction or reporting.
    ///
    /// @param account_id The account to calculate balance for.
    /// @param transactions All transactions for this account.
    /// @param currency The account's currency.
    /// @param cutoff_time Only transactions with occurred_at <= cutoff_time are included.
    /// @return BalanceSnapshot on success, DomainError on failure.
    [[nodiscard]] static DomainResult<BalanceSnapshot> calculate_balance_at(
        AccountId account_id,
        const std::vector<Transaction>& transactions,
        const Currency& currency,
        Transaction::TimePoint cutoff_time);

private:
    /// @brief Apply a single transaction to the running balance.
    ///
    /// Returns the signed delta to add to the balance based on transaction type.
    [[nodiscard]] static DomainResult<Money> apply_transaction(
        const Transaction& tx,
        const Currency& account_currency);
};

} // namespace pfh::domain
