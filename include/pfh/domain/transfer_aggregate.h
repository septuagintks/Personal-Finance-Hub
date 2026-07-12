// Personal Finance Hub - TransferAggregate
// Version: 1.0
// C++23
//
// TransferAggregate is the root of a transfer operation, which always consists
// of exactly two Transaction records (outgoing + incoming) linked by a
// transfer_group_id, both marked as TransactionType::Transfer. Optional
// adjustments (fees, FX loss/gain) are represented as separate Adjustment or
// Expense transactions.
//
// Three construction modes are supported:
// 1. Outgoing + Rate => Incoming (user specifies source amount and exchange rate)
// 2. Outgoing + Incoming => Rate (user specifies both amounts; rate is derived)
// 3. Incoming + Rate => Outgoing (user specifies target amount and exchange rate)
//
// The aggregate enforces consistency:
// - Same-currency: outgoing.amount == incoming.amount
// - Cross-currency: outgoing.amount * rate == incoming.amount (within rounding)

#pragma once

#include "pfh/domain/domain_error.h"
#include "pfh/domain/exchange_rate.h"
#include "pfh/domain/transaction.h"
#include <optional>
#include <vector>

namespace pfh::domain {

/// @brief How the transfer was constructed. Persisted so the exact input mode
/// can be reproduced (the repository must NOT infer it from currency equality).
enum class TransferMode {
    OutgoingAndRate = 1,   ///< User gave outgoing + rate; incoming derived.
    OutgoingAndIncoming = 2, ///< User gave both amounts; rate derived (or same-currency).
    IncomingAndRate = 3    ///< User gave incoming + rate; outgoing derived.
};

/// @brief Fee source for transfer adjustments.
enum class FeeSource {
    SourceAccount,  ///< Fee deducted from the source account (most common).
    TargetAccount,  ///< Fee deducted from the target account.
    ThirdParty      ///< Fee charged to a separate account (e.g., fee account).
};

/// @brief Transfer aggregate root — encapsulates a paired transfer.
///
/// A transfer consists of:
/// - Exactly one outgoing Transaction (from source account).
/// - Exactly one incoming Transaction (to target account).
/// - Optionally an ExchangeRate (for cross-currency transfers).
/// - Optionally adjustment Transactions (fees, FX loss/gain, rebates).
///
/// All transactions in the aggregate share the same transfer_group_id and are
/// created atomically.
class TransferAggregate {
public:
    [[nodiscard]] const Transaction& outgoing() const noexcept { return outgoing_; }
    [[nodiscard]] const Transaction& incoming() const noexcept { return incoming_; }
    [[nodiscard]] const std::optional<ExchangeRate>& rate() const noexcept { return rate_; }
    [[nodiscard]] const std::vector<Transaction>& adjustments() const noexcept { return adjustments_; }

    /// @brief The input mode used to construct this transfer (persisted).
    [[nodiscard]] TransferMode mode() const noexcept { return mode_; }

    /// @brief Check if this is a same-currency transfer.
    [[nodiscard]] bool is_same_currency() const noexcept {
        return outgoing_.amount().currency() == incoming_.amount().currency();
    }

    /// @brief Check if this is a cross-currency transfer.
    [[nodiscard]] bool is_cross_currency() const noexcept {
        return !is_same_currency();
    }

    /// @brief Get the transfer group ID linking the two sides.
    [[nodiscard]] std::optional<TransferGroupId> transfer_group_id() const noexcept {
        return outgoing_.transfer_group_id();
    }

private:
    /// @brief Construct a transfer aggregate.
    ///
    /// Private: only TransferDomainService may build an aggregate, so every
    /// aggregate has passed consistency validation and carries a real mode.
    /// This prevents callers from assembling an unvalidated/illegal transfer.
    TransferAggregate(
        TransferMode mode,
        Transaction outgoing,
        Transaction incoming,
        std::optional<ExchangeRate> rate,
        std::vector<Transaction> adjustments)
        : mode_(mode),
          outgoing_(std::move(outgoing)),
          incoming_(std::move(incoming)),
          rate_(std::move(rate)),
          adjustments_(std::move(adjustments)) {}

    friend class TransferDomainService;

    TransferMode mode_;
    Transaction outgoing_;
    Transaction incoming_;
    std::optional<ExchangeRate> rate_;
    std::vector<Transaction> adjustments_;
};

} // namespace pfh::domain
