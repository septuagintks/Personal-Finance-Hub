// Personal Finance Hub - Typed ID Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/domain/typed_id.h"
#include <gtest/gtest.h>
#include <unordered_set>

using namespace pfh::domain;

namespace pfh::test {

// Construction and basic operations
TEST(TypedId, WhenDefaultConstructed_IsInvalid) {
    UserId user_id;
    EXPECT_FALSE(user_id.is_valid());
    EXPECT_EQ(user_id.value(), 0);
}

TEST(TypedId, WhenConstructedWithValue_StoresValue) {
    UserId user_id(123);
    EXPECT_TRUE(user_id.is_valid());
    EXPECT_EQ(user_id.value(), 123);
}

TEST(TypedId, WhenZeroValue_IsInvalid) {
    AccountId account_id(0);
    EXPECT_FALSE(account_id.is_valid());
}

TEST(TypedId, WhenNegativeValue_IsValid) {
    // Negative IDs are technically valid (non-zero)
    // Though in practice we won't use negative IDs
    TransactionId tx_id(-1);
    EXPECT_TRUE(tx_id.is_valid());
    EXPECT_EQ(tx_id.value(), -1);
}

// Type safety
TEST(TypedId, WhenDifferentIdTypes_CannotBeCompared) {
    UserId user_id(1);
    AccountId account_id(1);

    // This should not compile - different types
    // EXPECT_EQ(user_id, account_id);  // Compilation error - good!

    // But same-typed IDs can be compared
    UserId another_user_id(1);
    EXPECT_EQ(user_id, another_user_id);
}

// Comparison operators
TEST(TypedId, WhenSameValue_AreEqual) {
    CategoryId cat1(42);
    CategoryId cat2(42);
    EXPECT_EQ(cat1, cat2);
    EXPECT_FALSE(cat1 != cat2);
}

TEST(TypedId, WhenDifferentValues_AreNotEqual) {
    TagId tag1(1);
    TagId tag2(2);
    EXPECT_NE(tag1, tag2);
    EXPECT_FALSE(tag1 == tag2);
}

TEST(TypedId, WhenCompared_SupportsOrdering) {
    ExchangeRateId id1(10);
    ExchangeRateId id2(20);
    ExchangeRateId id3(10);

    EXPECT_LT(id1, id2);
    EXPECT_LE(id1, id2);
    EXPECT_LE(id1, id3);
    EXPECT_GT(id2, id1);
    EXPECT_GE(id2, id1);
    EXPECT_GE(id1, id3);
}

// String conversion
TEST(TypedId, WhenConvertedToString_ReturnsValueAsString) {
    SyncJobId job_id(12345);
    EXPECT_EQ(job_id.to_string(), "12345");
}

TEST(TypedId, WhenInvalidId_ToStringReturnsZero) {
    AuditLogId log_id;
    EXPECT_EQ(log_id.to_string(), "0");
}

// Hash support
TEST(TypedId, WhenUsedInUnorderedSet_WorksCorrectly) {
    std::unordered_set<UserId> user_ids;

    UserId id1(1);
    UserId id2(2);
    UserId id3(1); // Duplicate of id1

    user_ids.insert(id1);
    user_ids.insert(id2);
    user_ids.insert(id3);

    EXPECT_EQ(user_ids.size(), 2); // Only 2 unique IDs
    EXPECT_TRUE(user_ids.contains(id1));
    EXPECT_TRUE(user_ids.contains(id2));
    EXPECT_TRUE(user_ids.contains(id3));
}

TEST(TypedId, WhenUsedAsMapKey_WorksCorrectly) {
    std::unordered_map<AccountId, std::string> account_names;

    AccountId acc1(100);
    AccountId acc2(200);

    account_names[acc1] = "Savings";
    account_names[acc2] = "Checking";

    EXPECT_EQ(account_names[acc1], "Savings");
    EXPECT_EQ(account_names[acc2], "Checking");
    EXPECT_EQ(account_names.size(), 2);
}

// Copy and move semantics
TEST(TypedId, WhenCopied_CreatesIndependentCopy) {
    TransactionId original(999);
    TransactionId copy = original;

    EXPECT_EQ(original, copy);
    EXPECT_EQ(original.value(), copy.value());
}

TEST(TypedId, WhenMoved_TransfersValue) {
    CategoryId original(888);
    CategoryId moved = std::move(original);

    EXPECT_EQ(moved.value(), 888);
    // Note: original is in valid but unspecified state after move
}

} // namespace pfh::test
