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

    // Determine the sign based on transaction type.
    // Convention: amount is stored as positive magnitude; we apply sign here.
    switch (tx.type()) {
    case TransactionType::Income:
        // Income adds to balance.
        return tx.amount();

    case TransactionType::Expense:
        // Expense subtracts from balance.
        return tx.amount().negated();

    case TransactionType::Transfer:
        // Transfer direction depends on whether this is the outgoing or incoming side.
        // The caller must filter transactions by account_id, so if this transaction
        // is for the current account:
        // - If it's the source account (outgoing), subtract.
        // - If it's the target account (incoming), add.
        //
        // However, we don't have account direction information in the Transaction
        // itself (only account_id). The architecture expects Transfer transactions
        // to already be partitioned correctly per account. For now, we adopt a
        // convention: Transfer transactions are stored with their natural sign
        // relative to the account they belong to. But since amount is always
        // positive, we need additional context.
        //
        // DESIGN DECISION: Transfer transactions should be created with a sign
        // convention OR the caller should pass direction context. For Phase 1
        // simplicity, we adopt: the repository or use case must ensure that
        // Transfer transactions for the source account are marked negative or
        // the service must receive direction info.
        //
        // REVISED: Transfer amounts are positive; the aggregate knows direction.
        // In balance calculation, we need to know if this is outgoing or incoming.
        // Since Transaction doesn't store direction explicitly, we infer from
        // transfer_group_id linkage (done by caller filtering).
        //
        // SIMPLIFICATION for Phase 1: Treat Transfer as context-dependent. The
        // caller (Application Use Case or Repository) must provide properly signed
        // transactions OR we adopt a convention that Transfer amounts in the
        // balance calculation are pre-signed by the repository query.
        //
        // For now, return the amount as-is and document that the caller must
        // provide signed Transfer amounts.
        return tx.amount();

    case TransactionType::Adjustment:
        // Adjustment can be positive (rebate, FX gain) or negative (fee, FX loss).
        // Stored as positive magnitude; sign determined by context. For Phase 1,
        // we treat Adjustment as always subtracting (most common: fees). The
        // application layer can model positive adjustments by creating them with
        // TransactionType::Income instead, or by storing a signed amount.
        //
        // REVISED: Return as-is and let the caller or domain rule decide. For now,
        // treat Adjustment as subtracting (fee convention).
        return tx.amount().negated();
    }

    // Unreachable (all enum cases covered), but satisfy compiler.
    return std::unexpected(DomainError::invalid_operation("Unknown transaction type"));
}

} // namespace pfh::domain
