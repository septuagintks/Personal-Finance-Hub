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
#include <optional>
#include <string>

namespace pfh::domain {

/// @brief Category board — matches DB enum category_board (income/expense only).
enum class CategoryBoard {
    Income,  ///< For Income transactions (salary, interest, refunds).
    Expense  ///< For Expense and fee-like Adjustment transactions.
};

/// @brief Category entity — organizes transactions.
class Category {
public:
    using TimePoint = std::chrono::system_clock::time_point;

    Category(
        CategoryId id,
        UserId owner,
        std::string name,
        CategoryBoard board,
        std::optional<CategoryId> parent_id = std::nullopt,
        std::string color = "#808080",
        std::string icon = "default",
        bool is_archived = false,
        TimePoint created_at = std::chrono::system_clock::now())
        : id_(id),
          owner_(owner),
          name_(std::move(name)),
          board_(board),
          parent_id_(parent_id),
          color_(std::move(color)),
          icon_(std::move(icon)),
          is_archived_(is_archived),
          created_at_(created_at) {}

    [[nodiscard]] CategoryId id() const noexcept { return id_; }
    [[nodiscard]] UserId owner() const noexcept { return owner_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] CategoryBoard board() const noexcept { return board_; }
    [[nodiscard]] const std::optional<CategoryId>& parent_id() const noexcept { return parent_id_; }
    [[nodiscard]] const std::string& color() const noexcept { return color_; }
    [[nodiscard]] const std::string& icon() const noexcept { return icon_; }
    [[nodiscard]] bool is_archived() const noexcept { return is_archived_; }
    [[nodiscard]] TimePoint created_at() const noexcept { return created_at_; }

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
    std::string color_;
    std::string icon_;
    bool is_archived_;
    TimePoint created_at_;
};

} // namespace pfh::domain
