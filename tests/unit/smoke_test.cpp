// Smoke Test - Verify GoogleTest is working
// Version: 1.0
// Naming convention: ClassName_WhenCondition_ExpectedBehavior

#include <gtest/gtest.h>

namespace pfh::test {

// Basic sanity check
TEST(SmokeTest, GoogleTestIsWorking) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(true);
}

// Demonstrate test naming convention
TEST(SmokeTest, WhenAddingPositiveNumbers_ReturnsCorrectSum) {
    int a = 5;
    int b = 3;
    EXPECT_EQ(a + b, 8);
}

// Demonstrate failure detection (should be commented out after verification)
// TEST(SmokeTest, WhenTestFails_GoogleTestDetectsIt) {
//     EXPECT_EQ(1, 2) << "This test should fail - uncomment to verify test failure detection";
// }

} // namespace pfh::test
