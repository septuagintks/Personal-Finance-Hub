// Personal Finance Hub - Error Handling Unit Tests
// Version: 1.0
// Test naming: ClassName_WhenCondition_ExpectedBehavior

#include "pfh/application/error.h"
#include <gtest/gtest.h>

using namespace pfh::application;
using namespace pfh::domain;

namespace pfh::test {

// Error construction
TEST(Error, WhenCreatedWithValidation_HasCorrectFields) {
    auto error = Error::validation("Invalid email format", "Email: invalid@");

    EXPECT_EQ(error.code, ErrorCode::ValidationError);
    EXPECT_EQ(error.message, "Invalid email format");
    EXPECT_EQ(error.details, "Email: invalid@");
}

TEST(Error, WhenCreatedWithNotFound_HasResourceInfo) {
    auto error = Error::not_found("Account", "12345");

    EXPECT_EQ(error.code, ErrorCode::NotFound);
    EXPECT_EQ(error.message, "Account not found");
    EXPECT_EQ(error.details, "ID: 12345");
}

TEST(Error, WhenCreatedWithDomainRuleViolation_HasCorrectCode) {
    auto error = Error::domain_rule_violation("Cannot transfer to archived account");

    EXPECT_EQ(error.code, ErrorCode::DomainRuleViolation);
    EXPECT_FALSE(error.message.empty());
}

// Result type - success cases
TEST(Result, WhenCreatedWithValue_IsSuccess) {
    Result<int> result = ok(42);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST(Result, WhenCreatedWithString_IsSuccess) {
    Result<std::string> result = ok(std::string("success"));

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "success");
}

TEST(VoidResult, WhenSuccessful_HasValue) {
    VoidResult result = ok();

    EXPECT_TRUE(result.has_value());
}

// Result type - error cases
TEST(Result, WhenCreatedWithError_IsError) {
    Result<int> result = err<int>(Error::validation("Invalid input"));

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
}

TEST(VoidResult, WhenError_HasNoValue) {
    VoidResult result = err_void(Error::unauthorized());

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::Unauthorized);
}

// Result pattern matching with expected
TEST(Result, WhenSuccess_CanAccessValue) {
    auto compute = []() -> Result<int> {
        return ok(100);
    };

    auto result = compute();

    if (result) {
        EXPECT_EQ(*result, 100);
    } else {
        FAIL() << "Should be success";
    }
}

TEST(Result, WhenError_CanAccessError) {
    auto compute = []() -> Result<int> {
        return err<int>(Error::not_found("User", "999"));
    };

    auto result = compute();

    if (!result) {
        EXPECT_EQ(result.error().code, ErrorCode::NotFound);
        EXPECT_EQ(result.error().message, "User not found");
    } else {
        FAIL() << "Should be error";
    }
}

// Result chaining
TEST(Result, WhenChained_PropagatesSuccess) {
    auto step1 = []() -> Result<int> { return ok(10); };
    auto step2 = [](int x) -> Result<int> { return ok(x * 2); };

    auto result = step1();
    if (result) {
        result = step2(*result);
    }

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 20);
}

TEST(Result, WhenChained_PropagatesError) {
    auto step1 = []() -> Result<int> {
        return err<int>(Error::validation("Step 1 failed"));
    };
    auto step2 = [](int x) -> Result<int> { return ok(x * 2); };

    auto result = step1();
    if (result) {
        result = step2(*result);
    }

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
}

// Domain error types
TEST(DomainError, WhenCreatedWithInvalidAmount_HasCorrectMessage) {
    auto error = DomainError::invalid_amount("negative value");

    EXPECT_EQ(error.code, DomainErrorCode::InvalidAmount);
    EXPECT_TRUE(error.message.find("negative value") != std::string::npos);
}

TEST(DomainError, WhenCurrencyMismatch_ContainsBothCurrencies) {
    auto error = DomainError::currency_mismatch("USD", "EUR");

    EXPECT_EQ(error.code, DomainErrorCode::CurrencyMismatch);
    EXPECT_TRUE(error.message.find("USD") != std::string::npos);
    EXPECT_TRUE(error.message.find("EUR") != std::string::npos);
}

TEST(DomainError, WhenExchangeRateNotFound_ContainsFromAndTo) {
    auto error = DomainError::exchange_rate_not_found("GBP", "JPY");

    EXPECT_EQ(error.code, DomainErrorCode::ExchangeRateNotFound);
    EXPECT_TRUE(error.message.find("GBP") != std::string::npos);
    EXPECT_TRUE(error.message.find("JPY") != std::string::npos);
}

// Domain result type
TEST(DomainResult, WhenSuccess_HasValue) {
    DomainResult<int> result = std::expected<int, DomainError>(42);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST(DomainResult, WhenError_HasError) {
    DomainResult<int> result = std::unexpected(
        DomainError::invalid_amount("test")
    );

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, DomainErrorCode::InvalidAmount);
}

} // namespace pfh::test
