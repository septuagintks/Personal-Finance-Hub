// Personal Finance Hub - TransferDomainService Implementation
// Version: 1.0
// C++23

#include "pfh/domain/transfer_domain_service.h"

namespace pfh::domain {

namespace {

/// @brief Helper to create a Transaction with Transfer type.
[[nodiscard]] Transaction make_transfer_transaction(
    TransactionId id,
    UserId user_id,
    AccountId account_id,
    Money amount,
    Transaction::TimePoint occurred_at,
    const std::string& description,
    TransferGroupId transfer_group_id) {
    return Transaction(
        id,
        user_id,
        account_id,
        amount,
        TransactionType::Transfer,
        occurred_at,
        description,
        std::nullopt,  // category_id (transfers don't use categories)
        transfer_group_id);
}

// NOTE: The domain service must NOT generate persistence IDs. It builds
// transactions with unassigned (invalid) ids; the repository / DB sequence
// assigns the real TransactionId and TransferGroupId at save time. This avoids
// non-atomic counters, restart resets, and collisions with existing sequences.

} // namespace

DomainResult<TransferAggregate> TransferDomainService::build_from_outgoing_and_rate(
    Money outgoing_amount,
    AccountId source_account,
    AccountId target_account,
    ExchangeRate rate,
    UserId user_id,
    Transaction::TimePoint occurred_at,
    std::string description,
    TransferGroupId transfer_group_id) {
    // Mode 1: Outgoing + Rate => Incoming.
    // Validate rate's base currency matches outgoing currency.
    if (!(outgoing_amount.currency() == rate.base())) {
        return std::unexpected(DomainError::currency_mismatch(
            outgoing_amount.currency().code(), rate.base().code()));
    }

    // Convert outgoing to incoming via the rate.
    auto incoming_result = rate.convert(outgoing_amount);
    if (!incoming_result) {
        return std::unexpected(incoming_result.error());
    }

    // Build the two transaction sides.
    auto outgoing_tx = make_transfer_transaction(
        TransactionId{},  // unassigned; repository assigns real id
        user_id,
        source_account,
        outgoing_amount,
        occurred_at,
        description,
        transfer_group_id);

    auto incoming_tx = make_transfer_transaction(
        TransactionId{},  // unassigned; repository assigns real id
        user_id,
        target_account,
        *incoming_result,
        occurred_at,
        description,
        transfer_group_id);

    TransferAggregate aggregate(
        std::move(outgoing_tx),
        std::move(incoming_tx),
        rate);

    // Validate consistency.
    if (auto validation = validate(aggregate); !validation) {
        return std::unexpected(validation.error());
    }

    return aggregate;
}

DomainResult<TransferAggregate> TransferDomainService::build_from_both_amounts(
    Money outgoing_amount,
    Money incoming_amount,
    AccountId source_account,
    AccountId target_account,
    UserId user_id,
    Transaction::TimePoint occurred_at,
    std::string description,
    TransferGroupId transfer_group_id) {
    // Mode 2: Outgoing + Incoming => derive Rate (or validate match for same-currency).
    const bool same_currency = (outgoing_amount.currency() == incoming_amount.currency());

    std::optional<ExchangeRate> rate;
    if (!same_currency) {
        // Derive the exchange rate: rate = incoming / outgoing.
        auto rate_decimal = incoming_amount.amount().divide(outgoing_amount.amount());
        if (!rate_decimal) {
            return std::unexpected(rate_decimal.error());
        }

        auto rate_result = ExchangeRate::create(
            outgoing_amount.currency(),
            incoming_amount.currency(),
            *rate_decimal,
            occurred_at,
            "DerivedFromAmounts");
        if (!rate_result) {
            return std::unexpected(rate_result.error());
        }
        rate = *rate_result;
    } else {
        // Same currency: amounts must match exactly.
        if (!(outgoing_amount == incoming_amount)) {
            return std::unexpected(DomainError::invalid_operation(
                "Same-currency transfer amounts must match"));
        }
    }

    // Build the two transaction sides.
    auto outgoing_tx = make_transfer_transaction(
        TransactionId{},  // unassigned; repository assigns real id
        user_id,
        source_account,
        outgoing_amount,
        occurred_at,
        description,
        transfer_group_id);

    auto incoming_tx = make_transfer_transaction(
        TransactionId{},  // unassigned; repository assigns real id
        user_id,
        target_account,
        incoming_amount,
        occurred_at,
        description,
        transfer_group_id);

    TransferAggregate aggregate(
        std::move(outgoing_tx),
        std::move(incoming_tx),
        rate);

    // Validate consistency.
    if (auto validation = validate(aggregate); !validation) {
        return std::unexpected(validation.error());
    }

    return aggregate;
}

DomainResult<TransferAggregate> TransferDomainService::build_from_incoming_and_rate(
    Money incoming_amount,
    AccountId source_account,
    AccountId target_account,
    ExchangeRate rate,
    UserId user_id,
    Transaction::TimePoint occurred_at,
    std::string description,
    TransferGroupId transfer_group_id) {
    // Mode 3: Incoming + Rate => Outgoing.
    // Validate rate's target currency matches incoming currency.
    if (!(incoming_amount.currency() == rate.target())) {
        return std::unexpected(DomainError::currency_mismatch(
            incoming_amount.currency().code(), rate.target().code()));
    }

    // Reverse the rate to get outgoing currency -> incoming currency.
    auto inverse_rate = rate.inverse();
    if (!inverse_rate) {
        return std::unexpected(inverse_rate.error());
    }

    // Convert incoming back to outgoing via the inverse rate.
    auto outgoing_result = inverse_rate->convert(incoming_amount);
    if (!outgoing_result) {
        return std::unexpected(outgoing_result.error());
    }

    // To ensure validation passes, recompute incoming from the computed outgoing
    // using the forward rate. This eliminates rounding discrepancies from the
    // inverse->forward round-trip.
    auto recomputed_incoming = rate.convert(*outgoing_result);
    if (!recomputed_incoming) {
        return std::unexpected(recomputed_incoming.error());
    }

    // Build the two transaction sides using the recomputed incoming amount.
    auto outgoing_tx = make_transfer_transaction(
        TransactionId{},  // unassigned; repository assigns real id
        user_id,
        source_account,
        *outgoing_result,
        occurred_at,
        description,
        transfer_group_id);

    auto incoming_tx = make_transfer_transaction(
        TransactionId{},  // unassigned; repository assigns real id
        user_id,
        target_account,
        *recomputed_incoming,  // Use recomputed amount for consistency
        occurred_at,
        description,
        transfer_group_id);

    TransferAggregate aggregate(
        std::move(outgoing_tx),
        std::move(incoming_tx),
        rate);

    // Validate consistency.
    if (auto validation = validate(aggregate); !validation) {
        return std::unexpected(validation.error());
    }

    return aggregate;
}

DomainVoidResult TransferDomainService::validate(const TransferAggregate& aggregate) {
    // Rule 1: Both transactions must be TransactionType::Transfer.
    if (aggregate.outgoing().type() != TransactionType::Transfer) {
        return std::unexpected(DomainError::invalid_operation(
            "Outgoing transaction must be TransactionType::Transfer"));
    }
    if (aggregate.incoming().type() != TransactionType::Transfer) {
        return std::unexpected(DomainError::invalid_operation(
            "Incoming transaction must be TransactionType::Transfer"));
    }

    // Rule 2: Same transfer_group_id.
    if (aggregate.outgoing().transfer_group_id() != aggregate.incoming().transfer_group_id()) {
        return std::unexpected(DomainError::invalid_operation(
            "Outgoing and incoming must share the same transfer_group_id"));
    }

    // Rule 3: Currency consistency.
    const auto& outgoing_amount = aggregate.outgoing().amount();
    const auto& incoming_amount = aggregate.incoming().amount();

    if (aggregate.is_same_currency()) {
        // Same currency: amounts must match exactly.
        if (!(outgoing_amount == incoming_amount)) {
            return std::unexpected(DomainError::invalid_operation(
                "Same-currency transfer: outgoing and incoming amounts must match"));
        }
        // Should have no rate.
        if (aggregate.rate().has_value()) {
            return std::unexpected(DomainError::invalid_operation(
                "Same-currency transfer should not have an exchange rate"));
        }
    } else {
        // Cross-currency: must have a rate, and outgoing * rate ≈ incoming.
        if (!aggregate.rate().has_value()) {
            return std::unexpected(DomainError::invalid_operation(
                "Cross-currency transfer must have an exchange rate"));
        }

        const auto& rate = *aggregate.rate();
        auto expected_incoming = rate.convert(outgoing_amount);
        if (!expected_incoming) {
            return std::unexpected(expected_incoming.error());
        }

        // Check if actual incoming matches expected within rounding tolerance.
        auto diff = incoming_amount.subtract(*expected_incoming);
        if (!diff) {
            return std::unexpected(diff.error());
        }

        auto tolerance = Decimal::parse(kRoundingTolerance);
        if (!tolerance) {
            return std::unexpected(tolerance.error());
        }

        if (diff->amount().abs() > *tolerance) {
            return std::unexpected(DomainError::invalid_operation(
                "Cross-currency transfer: incoming amount does not match outgoing * rate within rounding tolerance"));
        }
    }

    return {};
}

} // namespace pfh::domain
