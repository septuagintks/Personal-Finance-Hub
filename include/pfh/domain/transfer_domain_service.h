// Personal Finance Hub - TransferDomainService
// Version: 1.0
// C++23
//
// TransferDomainService is a pure domain service responsible for constructing
// and validating TransferAggregate instances. It enforces transfer consistency
// rules but does NOT access repositories, open transactions, or publish events.
//
// Key rules enforced:
// 1. Both transactions must be marked as TransactionType::Transfer.
// 2. Same-currency: amounts must match exactly.
// 3. Cross-currency: outgoing * rate ≈ incoming (within rounding tolerance).
// 4. Fees and FX loss/gain must be represented as separate Adjustment transactions.

#pragma once

#include "pfh/domain/currency_conversion_service.h"
#include "pfh/domain/domain_error.h"
#include "pfh/domain/transfer_aggregate.h"
#include <optional>
#include <string>

namespace pfh::domain {

/// @brief Pure domain service for transfer construction and validation.
///
/// This service does NOT:
/// - Call repositories
/// - Open database transactions
/// - Publish domain events
///
/// Those responsibilities belong to the Application Use Case layer.
class TransferDomainService {
public:
    /// @brief Build a transfer from outgoing amount and exchange rate.
    ///
    /// Mode 1: User specifies source amount and rate; incoming is calculated.
    ///
    /// @param outgoing_amount Source account amount (e.g., 1000 USD).
    /// @param source_account Source account ID.
    /// @param target_account Target account ID.
    /// @param rate Exchange rate (1 outgoing currency = rate target currency).
    /// @param user_id Owning user.
    /// @param occurred_at Transaction timestamp.
    /// @param description Optional description.
    /// @param transfer_group_id Link between the two sides.
    /// @param fee Optional resolved fee, expressed as a positive magnitude.
    /// @return TransferAggregate on success, DomainError on validation failure.
    [[nodiscard]] static DomainResult<TransferAggregate> build_from_outgoing_and_rate(
        Money outgoing_amount,
        AccountId source_account,
        AccountId target_account,
        ExchangeRate rate,
        UserId user_id,
        Transaction::TimePoint occurred_at,
        std::string description,
        TransferGroupId transfer_group_id,
        std::optional<TransferFee> fee = std::nullopt);

    /// @brief Build a transfer from both amounts; derive the exchange rate.
    ///
    /// Mode 2: User specifies both outgoing and incoming amounts; rate is
    /// calculated. If same-currency, amounts must match; if cross-currency, the
    /// implied rate is recorded.
    ///
    /// @param outgoing_amount Source account amount.
    /// @param incoming_amount Target account amount.
    /// @param source_account Source account ID.
    /// @param target_account Target account ID.
    /// @param user_id Owning user.
    /// @param occurred_at Transaction timestamp.
    /// @param description Optional description.
    /// @param transfer_group_id Link between the two sides.
    /// @param fee Optional resolved fee, expressed as a positive magnitude.
    /// @return TransferAggregate on success, DomainError on validation failure.
    [[nodiscard]] static DomainResult<TransferAggregate> build_from_both_amounts(
        Money outgoing_amount,
        Money incoming_amount,
        AccountId source_account,
        AccountId target_account,
        UserId user_id,
        Transaction::TimePoint occurred_at,
        std::string description,
        TransferGroupId transfer_group_id,
        std::optional<TransferFee> fee = std::nullopt);

    /// @brief Build a transfer from incoming amount and exchange rate.
    ///
    /// Mode 3: User specifies target amount and rate; outgoing is calculated.
    ///
    /// @param incoming_amount Target account amount (e.g., 7180 CNY).
    /// @param source_account Source account ID.
    /// @param target_account Target account ID.
    /// @param rate Exchange rate (1 outgoing currency = rate target currency).
    /// @param user_id Owning user.
    /// @param occurred_at Transaction timestamp.
    /// @param description Optional description.
    /// @param transfer_group_id Link between the two sides.
    /// @param fee Optional resolved fee, expressed as a positive magnitude.
    /// @return TransferAggregate on success, DomainError on validation failure.
    [[nodiscard]] static DomainResult<TransferAggregate> build_from_incoming_and_rate(
        Money incoming_amount,
        AccountId source_account,
        AccountId target_account,
        ExchangeRate rate,
        UserId user_id,
        Transaction::TimePoint occurred_at,
        std::string description,
        TransferGroupId transfer_group_id,
        std::optional<TransferFee> fee = std::nullopt);

    /// @brief Validate transfer consistency rules.
    ///
    /// Checks:
    /// - Both transactions are TransactionType::Transfer.
    /// - Same transfer_group_id.
    /// - Same-currency: amounts match.
    /// - Cross-currency: outgoing * rate ≈ incoming (within rounding).
    /// - Adjustments are signed, non-zero, same-user members of the group.
    ///
    /// @param aggregate The transfer to validate.
    /// @return Success (empty) or DomainError describing the violation.
    [[nodiscard]] static DomainVoidResult validate(const TransferAggregate& aggregate);

private:
    // Persisted amounts use scale 8 and rates use scale 10. Validation accounts
    // for both explicit amount rounding and rate-derivation rounding.
    static constexpr const char* kAmountRoundingUnit = "0.00000001";
    static constexpr const char* kRateRoundingUnit = "0.0000000001";
};

} // namespace pfh::domain
