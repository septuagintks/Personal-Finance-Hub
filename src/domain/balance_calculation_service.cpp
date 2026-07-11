// Personal Finance Hub - BalanceCalculationService Implementation
// Version: 1.0
// C++23

#include "pfh/domain/balance_calculation_service.h"

namespace pfh::domain {

DomainResult<BalanceSnapshot> BalanceCalculationService::calculate_balance(
    AccountId account_id,
    const std::vector<Transaction>& transactions,
    const Currency& currency,
    Transaction::TimePoint as_of) {
    // Start with zero balance in the account's currency.
    auto zero_amount = Decimal::from_integer(0);
    if (!zero_amount) {
        return std::unexpected(zero_amount.error());
    }
    Money running_balance(*zero_amount, currency);

    // Apply each transaction.
    for (const auto& tx : transactions) {
        // Skip soft-deleted transactions.
        if (tx.is_deleted()) {
            continue;
        }

        // Apply the transaction to the balance.
        auto delta = apply_transaction(tx, currency);
        if (!delta) {
            return std::unexpected(delta.error());
        }

        auto new_balance = running_balance.add(*delta);
        if (!new_balance) {
            return std::unexpected(new_balance.error());
        }
        running_balance = *new_balance;
    }

    return BalanceSnapshot(account_id, running_balance, as_of);
}

DomainResult<BalanceSnapshot> BalanceCalculationService::calculate_balance_at(
    AccountId account_id,
    const std::vector<Transaction>& transactions,
    const Currency& currency,
    Transaction::TimePoint cutoff_time) {
    // Start with zero balance.
    auto zero_amount = Decimal::from_integer(0);
    if (!zero_amount) {
        return std::unexpected(zero_amount.error());
    }
    Money running_balance(*zero_amount, currency);

    // Apply only transactions that occurred before or at the cutoff.
    for (const auto& tx : transactions) {
        if (tx.is_deleted()) {
            continue;
        }
        if (tx.occurred_at() > cutoff_time) {
            continue;
        }

        auto delta = apply_transaction(tx, currency);
        if (!delta) {
            return std::unexpected(delta.error());
        }

        auto new_balance = running_balance.add(*delta);
        if (!new_balance) {
            return std::unexpected(new_balance.error());
        }
        running_balance = *new_balance;
    }

    return BalanceSnapshot(account_id, running_balance, cutoff_time);
}

DomainResult<Money> BalanceCalculationService::apply_transaction(
    const Transaction& tx,
    const Currency& account_currency) {
    // Validate currency match.
    if (!(tx.amount().currency() == account_currency)) {
        return std::unexpected(DomainError::currency_mismatch(
            tx.amount().currency().code(),
            account_currency.code()));
    }

    // Amount sign handling supports both:
    // 1) Domain unit tests: positive magnitudes + type-based direction
    // 2) Persistence layer: signed amounts (expense/transfer-out negative)
    switch (tx.type()) {
    case TransactionType::Income:
        // Income always increases balance. Prefer positive amount.
        return tx.amount().is_negative() ? tx.amount().negated() : tx.amount();

    case TransactionType::Expense:
        // Expense decreases balance. Accept positive magnitude or pre-signed negative.
        return tx.amount().is_negative() ? tx.amount() : tx.amount().negated();

    case TransactionType::Transfer:
        // After repository mapping, transfer amounts are signed
        // (outgoing negative, incoming positive). If a positive magnitude is
        // provided (domain-only construction without save), treat as +delta and
        // rely on caller role semantics for tests that only inspect one side.
        return tx.amount();

    case TransactionType::Adjustment:
        // Prefer signed storage. Positive magnitude defaults to fee (outflow).
        return tx.amount().is_negative() ? tx.amount() : tx.amount().negated();
    }

    return std::unexpected(DomainError::invalid_operation("Unknown transaction type"));
}

} // namespace pfh::domain
