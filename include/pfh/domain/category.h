// Personal Finance Hub - Category Entity
// Version: 1.1
// C++23
//
// Category organizes transactions into income/expense boards. Board values
// match PostgreSQL category_board enum: 'income' | 'expense' only.
//
// Key rules (aligned with 02_Database_Design.md):
// - Income transactions can only use Income board categories.
// - Expense transactions can only use Expense board categories.
// - Fee-like Adjustment transactions use Expense board categories.
// - Transfer transactions do NOT use categories (transfer_group_id instead).

#pragma once

#include "pfh/domain/domain_error.h"
#include "pfh/domain/transaction.h"
#include "pfh/domain/typed_id.h"
#include "pfh/domain/user.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace pfh::domain {

/// @brief Category board — matches DB enum category_board (income/expense only).
enum class CategoryBoard {
    Income,  ///< For Income transactions (salary, interest, refunds).
    Expense  ///< For Expense and fee-like Adjustment transactions.
};

/// @brief Category origin — matches DB enum category_source.
enum class CategorySource {
    System, ///< Materialized from a system_category_templates row.
    User    ///< Created directly by the user.
};

/// @brief Category entity — organizes transactions.
///
/// Fields mirror the `categories` table (02_Database_Design / V1 schema):
/// board, source, template_id, sort_order, deleted_at. Presentation-only
/// attributes such as colour/icon are NOT persisted here; the UI derives or
/// stores them separately, so they are intentionally absent from the entity.
class Category {
public:
    using TimePoint = std::chrono::system_clock::time_point;

    Category(
        CategoryId id,
        UserId owner,
        std::string name,
        CategoryBoard board,
        std::optional<CategoryId> parent_id = std::nullopt,
        CategorySource source = CategorySource::User,
        std::optional<std::int64_t> template_id = std::nullopt,
        int sort_order = 0,
        std::optional<TimePoint> deleted_at = std::nullopt,
        TimePoint created_at = std::chrono::system_clock::now(),
        TimePoint updated_at = std::chrono::system_clock::now())
        : id_(id),
          owner_(owner),
          name_(std::move(name)),
          board_(board),
          parent_id_(parent_id),
          source_(source),
          template_id_(template_id),
          sort_order_(sort_order),
          deleted_at_(deleted_at),
          created_at_(created_at),
          updated_at_(updated_at) {}

    [[nodiscard]] CategoryId id() const noexcept { return id_; }
    [[nodiscard]] UserId owner() const noexcept { return owner_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] CategoryBoard board() const noexcept { return board_; }
    [[nodiscard]] const std::optional<CategoryId>& parent_id() const noexcept { return parent_id_; }
    [[nodiscard]] CategorySource source() const noexcept { return source_; }
    [[nodiscard]] const std::optional<std::int64_t>& template_id() const noexcept { return template_id_; }
    [[nodiscard]] int sort_order() const noexcept { return sort_order_; }
    [[nodiscard]] const std::optional<TimePoint>& deleted_at() const noexcept { return deleted_at_; }
    [[nodiscard]] bool is_deleted() const noexcept { return deleted_at_.has_value(); }
    [[nodiscard]] TimePoint created_at() const noexcept { return created_at_; }
    [[nodiscard]] TimePoint updated_at() const noexcept { return updated_at_; }

    /// @brief True if this is a top-level (first-level) category.
    [[nodiscard]] bool is_root() const noexcept { return !parent_id_.has_value(); }

    /// @brief Check if this category can be used by a given transaction type.
    [[nodiscard]] bool is_valid_for(TransactionType tx_type) const noexcept {
        switch (tx_type) {
        case TransactionType::Income:
            return board_ == CategoryBoard::Income;
        case TransactionType::Expense:
        case TransactionType::Adjustment:
            return board_ == CategoryBoard::Expense;
        case TransactionType::Transfer:
            return false;
        }
        return false;
    }

    /// @brief Validate that a category board can be assigned to a transaction type.
    [[nodiscard]] static DomainVoidResult validate_category_board(
        TransactionType tx_type,
        CategoryBoard category_board) {
        Category dummy_category(
            CategoryId(0),
            UserId(0),
            "",
            category_board);

        if (!dummy_category.is_valid_for(tx_type)) {
            const std::string board_name =
                (category_board == CategoryBoard::Income) ? "Income" : "Expense";

            std::string tx_type_name;
            switch (tx_type) {
            case TransactionType::Income: tx_type_name = "Income"; break;
            case TransactionType::Expense: tx_type_name = "Expense"; break;
            case TransactionType::Adjustment: tx_type_name = "Adjustment"; break;
            case TransactionType::Transfer: tx_type_name = "Transfer"; break;
            }

            return std::unexpected(DomainError::invalid_operation(
                tx_type_name + " transactions cannot use " + board_name +
                " board categories"));
        }

        return {};
    }

private:
    CategoryId id_;
    UserId owner_;
    std::string name_;
    CategoryBoard board_;
    std::optional<CategoryId> parent_id_;
    CategorySource source_;
    std::optional<std::int64_t> template_id_;
    int sort_order_;
    std::optional<TimePoint> deleted_at_;
    TimePoint created_at_;
    TimePoint updated_at_;
};

} // namespace pfh::domain
