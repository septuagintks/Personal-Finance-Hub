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
        TransferMode::OutgoingAndRate,
        std::move(outgoing_tx),
        std::move(incoming_tx),
        rate,
        {});

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
        TransferMode::OutgoingAndIncoming,
        std::move(outgoing_tx),
        std::move(incoming_tx),
        rate,
        {});

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

    // Derive outgoing = incoming / rate by DIRECT division (not inverse-then-
    // multiply, which loses precision from the intermediate inverse rate). The
    // user's incoming amount is authoritative and is NEVER modified: e.g. an
    // input of 7180 CNY stays exactly 7180 CNY, and outgoing = 7180 / 7.18 =
    // 1000 USD. Any residual rounding lands on the derived (outgoing) side and
    // is absorbed by the cross-currency tolerance check in validate().
    auto outgoing_decimal = incoming_amount.amount().divide(rate.rate());
    if (!outgoing_decimal) {
        return std::unexpected(outgoing_decimal.error());
    }
    Money outgoing_amount(*outgoing_decimal, rate.base());

    // Build the two transaction sides. Incoming keeps the user's exact input.
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
        incoming_amount,  // user's exact input, preserved
        occurred_at,
        description,
        transfer_group_id);

    TransferAggregate aggregate(
        TransferMode::IncomingAndRate,
        std::move(outgoing_tx),
        std::move(incoming_tx),
        rate,
        {});

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

    // Rule 3: Source and target accounts must differ. A self-transfer is not a
    // transfer. Enforced here so the invariant holds no matter which builder or
    // caller constructed the aggregate (not only CreateTransferUseCase).
    if (aggregate.outgoing().account_id() == aggregate.incoming().account_id()) {
        return std::unexpected(DomainError::invalid_operation(
            "Transfer source and target accounts must differ"));
    }

    // Rule 4: Both legs must belong to the same user.
    if (aggregate.outgoing().user_id() != aggregate.incoming().user_id()) {
        return std::unexpected(DomainError::invalid_operation(
            "Transfer legs must belong to the same user"));
    }

    // Rule 5: Both amounts must be strictly positive magnitudes. The domain
    // builds legs with positive magnitudes (the repository applies storage
    // signs), so a non-positive amount here is an illegal transfer.
    if (!aggregate.outgoing().amount().amount().is_positive() ||
        !aggregate.incoming().amount().amount().is_positive()) {
        return std::unexpected(DomainError::invalid_operation(
            "Transfer amounts must be positive"));
    }

    // Rule 6: Currency consistency.
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

        auto base_tolerance = Decimal::parse(kRoundingTolerance);
        if (!base_tolerance) {
            return std::unexpected(base_tolerance.error());
        }

        // Scale the tolerance by the rate. When outgoing is derived from
        // incoming / rate (Mode 3), its ≤0.5-ULP rounding is amplified by the
        // rate when we recompute expected_incoming = outgoing * rate, so the
        // round-trip drift is bounded by ~(rate + 1) ULP, not a fixed 1 ULP.
        // A genuinely mismatched incoming is off by orders of magnitude more,
        // so this stays safe against real errors.
        auto one = Decimal::from_integer(1);
        if (!one) {
            return std::unexpected(one.error());
        }
        auto rate_plus_one = rate.rate().abs().add(*one);
        if (!rate_plus_one) {
            return std::unexpected(rate_plus_one.error());
        }
        auto tolerance = base_tolerance->multiply(*rate_plus_one);
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
