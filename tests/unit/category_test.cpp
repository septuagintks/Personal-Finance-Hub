// Personal Finance Hub - Category Unit Tests
// Version: 1.1
// Board model matches DB category_board: income | expense only.
// Fee-like Adjustment transactions use Expense board.

#include "pfh/domain/category.h"
#include "test_support.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

TEST(Category, WhenIncomeCategoryUsedForIncome_Validates) {
    auto result = Category::validate_category_board(
        TransactionType::Income,
        CategoryBoard::Income);
    EXPECT_TRUE(result.has_value());
}

TEST(Category, WhenExpenseCategoryUsedForExpense_Validates) {
    auto result = Category::validate_category_board(
        TransactionType::Expense,
        CategoryBoard::Expense);
    EXPECT_TRUE(result.has_value());
}

TEST(Category, WhenExpenseCategoryUsedForAdjustment_Validates) {
    auto result = Category::validate_category_board(
        TransactionType::Adjustment,
        CategoryBoard::Expense);
    EXPECT_TRUE(result.has_value());
}

TEST(Category, WhenIncomeCategoryUsedForExpense_ReturnsError) {
    auto result = Category::validate_category_board(
        TransactionType::Expense,
        CategoryBoard::Income);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::InvalidOperation);
}

TEST(Category, WhenExpenseCategoryUsedForIncome_ReturnsError) {
    auto result = Category::validate_category_board(
        TransactionType::Income,
        CategoryBoard::Expense);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::InvalidOperation);
}

TEST(Category, WhenIncomeCategoryUsedForAdjustment_ReturnsError) {
    auto result = Category::validate_category_board(
        TransactionType::Adjustment,
        CategoryBoard::Income);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::InvalidOperation);
}

TEST(Category, WhenAnyCategoryUsedForTransfer_ReturnsError) {
    auto result = Category::validate_category_board(
        TransactionType::Transfer,
        CategoryBoard::Income);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::InvalidOperation);

    result = Category::validate_category_board(
        TransactionType::Transfer,
        CategoryBoard::Expense);
    ASSERT_FALSE(result.has_value());
}

TEST(Category, WhenCategoryBoardMatchesTransactionType_IsValidForReturnsTrue) {
    Category income_cat(CategoryId(1), UserId(100), "Salary", CategoryBoard::Income);
    EXPECT_TRUE(income_cat.is_valid_for(TransactionType::Income));
    EXPECT_FALSE(income_cat.is_valid_for(TransactionType::Expense));
    EXPECT_FALSE(income_cat.is_valid_for(TransactionType::Adjustment));
    EXPECT_FALSE(income_cat.is_valid_for(TransactionType::Transfer));

    Category expense_cat(CategoryId(2), UserId(100), "Groceries", CategoryBoard::Expense);
    EXPECT_TRUE(expense_cat.is_valid_for(TransactionType::Expense));
    EXPECT_TRUE(expense_cat.is_valid_for(TransactionType::Adjustment));
    EXPECT_FALSE(expense_cat.is_valid_for(TransactionType::Income));
}

TEST(Category, WhenConstructedWithDefaults_HasExpectedProperties) {
    Category cat(CategoryId(1), UserId(100), "Test Category", CategoryBoard::Income);
    EXPECT_EQ(cat.id().value(), 1);
    EXPECT_EQ(cat.owner().value(), 100);
    EXPECT_EQ(cat.name(), "Test Category");
    EXPECT_EQ(cat.board(), CategoryBoard::Income);
    EXPECT_FALSE(cat.parent_id().has_value());
    // Aligned with schema: source/template_id/sort_order/deleted_at, no color/icon.
    EXPECT_EQ(cat.source(), CategorySource::User);
    EXPECT_FALSE(cat.template_id().has_value());
    EXPECT_EQ(cat.sort_order(), 0);
    EXPECT_FALSE(cat.is_deleted());
    EXPECT_TRUE(cat.is_root());
    EXPECT_GT(cat.updated_at(), std::chrono::system_clock::time_point{});
}

TEST(Category, WhenSystemSourced_ExposesTemplateAndSource) {
    Category cat(
        CategoryId(5), UserId(100), "\xe9\xa4\x90\xe9\xa5\xae", CategoryBoard::Expense,
        std::nullopt, CategorySource::System, std::int64_t{42}, 3);
    EXPECT_EQ(cat.source(), CategorySource::System);
    ASSERT_TRUE(cat.template_id().has_value());
    EXPECT_EQ(*cat.template_id(), 42);
    EXPECT_EQ(cat.sort_order(), 3);
    EXPECT_TRUE(cat.is_root());
}

TEST(Category, WhenConstructedWithParent_HasParentId) {
    Category parent(CategoryId(1), UserId(100), "Parent", CategoryBoard::Expense);
    Category child(
        CategoryId(2),
        UserId(100),
        "Child",
        CategoryBoard::Expense,
        CategoryId(1));
    EXPECT_TRUE(child.parent_id().has_value());
    EXPECT_EQ(child.parent_id()->value(), 1);
}

} // namespace pfh::test
