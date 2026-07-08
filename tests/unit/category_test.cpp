// Personal Finance Hub - Category Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/category.h"
#include "test_support.h"
#include <gtest/gtest.h>

using namespace pfh::domain;

namespace pfh::test {

// ---- Category board validation ----

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

TEST(Category, WhenAdjustmentCategoryUsedForAdjustment_Validates) {
    auto result = Category::validate_category_board(
        TransactionType::Adjustment,
        CategoryBoard::Adjustment);
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

    result = Category::validate_category_board(
        TransactionType::Transfer,
        CategoryBoard::Adjustment);
    ASSERT_FALSE(result.has_value());
}

// ---- is_valid_for instance method ----

TEST(Category, WhenCategoryBoardMatchesTransactionType_IsValidForReturnsTrue) {
    Category income_cat(CategoryId(1), UserId(100), "Salary", CategoryBoard::Income);
    EXPECT_TRUE(income_cat.is_valid_for(TransactionType::Income));
    EXPECT_FALSE(income_cat.is_valid_for(TransactionType::Expense));
    EXPECT_FALSE(income_cat.is_valid_for(TransactionType::Adjustment));
    EXPECT_FALSE(income_cat.is_valid_for(TransactionType::Transfer));

    Category expense_cat(CategoryId(2), UserId(100), "Groceries", CategoryBoard::Expense);
    EXPECT_TRUE(expense_cat.is_valid_for(TransactionType::Expense));
    EXPECT_FALSE(expense_cat.is_valid_for(TransactionType::Income));

    Category adjustment_cat(CategoryId(3), UserId(100), "Bank Fee", CategoryBoard::Adjustment);
    EXPECT_TRUE(adjustment_cat.is_valid_for(TransactionType::Adjustment));
    EXPECT_FALSE(adjustment_cat.is_valid_for(TransactionType::Income));
}

// ---- Category construction ----

TEST(Category, WhenConstructedWithDefaults_HasExpectedProperties) {
    Category cat(CategoryId(1), UserId(100), "Test Category", CategoryBoard::Income);
    EXPECT_EQ(cat.id().value(), 1);
    EXPECT_EQ(cat.owner().value(), 100);
    EXPECT_EQ(cat.name(), "Test Category");
    EXPECT_EQ(cat.board(), CategoryBoard::Income);
    EXPECT_FALSE(cat.parent_id().has_value());
    EXPECT_EQ(cat.color(), "#808080");
    EXPECT_EQ(cat.icon(), "default");
    EXPECT_FALSE(cat.is_archived());
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
