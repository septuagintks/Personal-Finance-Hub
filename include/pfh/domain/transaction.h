// Personal Finance Hub - Transaction Entity
// Version: 1.0
// C++23
//
// Transaction is the core financial event entity. Transfers produce exactly two
// Transaction records (outgoing + incoming) linked by transfer_group_id, both
// marked as TransactionType::Transfer. Income, Expense, and Adjustment are
// standalone transactions.

#pragma once

#include "pfh/domain/account.h"
#include "pfh/domain/money.h"
#include "pfh/domain/typed_id.h"
#include "pfh/domain/user.h"
#include <chrono>
#include <optional>
#include <string>

namespace pfh::domain {

/// @brief Tag for TransferGroupId strong typing.
struct TransferGroupIdTag {};

/// @brief Strongly-typed transaction identifier.
using TransactionId = TypedId<TransactionIdTag>;

/// @brief Strongly-typed category identifier.
using CategoryId = TypedId<CategoryIdTag>;

/// @brief Strongly-typed transfer group identifier.
///
/// Transfer groups link the outgoing and incoming sides of a transfer. The
/// database uses a BIGSERIAL sequence (aligned with the int64 TypedId model);
/// the repository / DB sequence assigns the id at save time. Domain code never
/// generates persistence ids itself.
using TransferGroupId = TypedId<TransferGroupIdTag>;

/// @brief Transaction type classification.
enum class TransactionType {
    Income,      ///< Money coming into an account (salary, interest, refund).
    Expense,     ///< Money leaving an account (purchase, rent, tax).
    Transfer,    ///< Money moving between user accounts (marked on both sides).
    Adjustment   ///< Corrections, fees, rebates, FX loss/gain.
};

/// @brief Transaction entity — the core financial event.
///
/// - Transfer transactions are always created in pairs (outgoing + incoming),
///   linked by transfer_group_id, and both marked as TransactionType::Transfer.
/// - Income/Expense/Adjustment are standalone.
/// - Amount sign conventions:
///   * Domain construction (TransferDomainService) uses positive magnitudes and
///     encodes direction via roles (outgoing/incoming).
///   * Persistence (Repository) stores signed amounts matching PostgreSQL:
///     income > 0, expense < 0, transfer outgoing < 0, transfer incoming > 0,
///     adjustment signed by business meaning.
///   * BalanceCalculationService:
///     - Income: +amount (expects positive magnitude or positive signed)
///     - Expense: -amount when magnitude positive
///     - Transfer: use amount as signed (after repository mapping)
///     - Adjustment: fee-default negate when positive
/// - Soft deletion: deleted_at is set rather than removing the record.
class Transaction {
public:
    using TimePoint = std::chrono::system_clock::time_point;

    Transaction(
        TransactionId id,
        UserId user_id,
        AccountId account_id,
        Money amount,
        TransactionType type,
        TimePoint occurred_at,
        std::string description = "",
        std::optional<CategoryId> category_id = std::nullopt,
        std::optional<TransferGroupId> transfer_group_id = std::nullopt,
        TimePoint created_at = std::chrono::system_clock::now(),
        std::optional<TimePoint> deleted_at = std::nullopt)
        : id_(id),
          user_id_(user_id),
          account_id_(account_id),
          amount_(amount),
          type_(type),
          occurred_at_(occurred_at),
          description_(std::move(description)),
          category_id_(category_id),
          transfer_group_id_(transfer_group_id),
          created_at_(created_at),
          deleted_at_(deleted_at) {}

    [[nodiscard]] TransactionId id() const noexcept { return id_; }
    [[nodiscard]] UserId user_id() const noexcept { return user_id_; }
    [[nodiscard]] AccountId account_id() const noexcept { return account_id_; }
    [[nodiscard]] const Money& amount() const noexcept { return amount_; }
    [[nodiscard]] TransactionType type() const noexcept { return type_; }
    [[nodiscard]] TimePoint occurred_at() const noexcept { return occurred_at_; }
    [[nodiscard]] const std::string& description() const noexcept { return description_; }
    [[nodiscard]] const std::optional<CategoryId>& category_id() const noexcept { return category_id_; }
    [[nodiscard]] const std::optional<TransferGroupId>& transfer_group_id() const noexcept { return transfer_group_id_; }
    [[nodiscard]] TimePoint created_at() const noexcept { return created_at_; }
    [[nodiscard]] const std::optional<TimePoint>& deleted_at() const noexcept { return deleted_at_; }

    /// @brief Check if this transaction has been soft-deleted.
    [[nodiscard]] bool is_deleted() const noexcept { return deleted_at_.has_value(); }

    /// @brief Soft-delete this transaction (set deleted_at timestamp).
    void mark_deleted(TimePoint deleted_at) {
        deleted_at_ = deleted_at;
    }

private:
    TransactionId id_;
    UserId user_id_;
    AccountId account_id_;
    Money amount_;
    TransactionType type_;
    TimePoint occurred_at_;
    std::string description_;
    std::optional<CategoryId> category_id_;
    std::optional<TransferGroupId> transfer_group_id_;
    TimePoint created_at_;
    std::optional<TimePoint> deleted_at_;
};

} // namespace pfh::domain
