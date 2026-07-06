// Personal Finance Hub - Error Types and Result Handling
// Version: 1.0
// C++23
// This file defines application-layer error types and result handling

#pragma once

#include <expected>
#include <string>
#include <system_error>

namespace pfh::application {

/// @brief Application-layer error categories
enum class ErrorCode {
    // Validation errors (400)
    ValidationError,
    InvalidInput,
    MissingRequiredField,
    InvalidFormat,

    // Authentication errors (401)
    Unauthorized,
    InvalidToken,
    ExpiredToken,

    // Authorization errors (403)
    Forbidden,
    InsufficientPermissions,

    // Resource errors (404)
    NotFound,
    UserNotFound,
    AccountNotFound,
    TransactionNotFound,
    CategoryNotFound,

    // Conflict errors (409)
    Conflict,
    DuplicateResource,
    VersionMismatch,
    OptimisticLockFailure,

    // Business rule errors (422)
    DomainRuleViolation,
    InvalidCurrencyOperation,
    InsufficientBalance,
    TransferAmountMismatch,
    InvalidExchangeRate,
    CrossCurrencyWithoutRate,
    CategoryBoardMismatch,
    ArchivedAccountOperation,

    // Infrastructure errors (500)
    InfrastructureFailure,
    DatabaseError,
    DatabaseConnectionFailed,
    TransactionFailed,
    ExternalServiceError,
    ConfigurationError,

    // Internal errors (500)
    InternalError,
    UnexpectedError
};

/// @brief Detailed error information
struct Error {
    ErrorCode code;
    std::string message;
    std::string details; // Additional context for debugging

    Error(ErrorCode code, std::string message, std::string details = "")
        : code(code), message(std::move(message)), details(std::move(details)) {}

    /// @brief Create a validation error
    [[nodiscard]] static Error validation(std::string message, std::string details = "") {
        return Error(ErrorCode::ValidationError, std::move(message), std::move(details));
    }

    /// @brief Create an unauthorized error
    [[nodiscard]] static Error unauthorized(std::string message = "Unauthorized") {
        return Error(ErrorCode::Unauthorized, std::move(message));
    }

    /// @brief Create a forbidden error
    [[nodiscard]] static Error forbidden(std::string message = "Forbidden") {
        return Error(ErrorCode::Forbidden, std::move(message));
    }

    /// @brief Create a not found error
    [[nodiscard]] static Error not_found(std::string resource_type, std::string resource_id) {
        return Error(ErrorCode::NotFound,
                    resource_type + " not found",
                    "ID: " + resource_id);
    }

    /// @brief Create a conflict error
    [[nodiscard]] static Error conflict(std::string message, std::string details = "") {
        return Error(ErrorCode::Conflict, std::move(message), std::move(details));
    }

    /// @brief Create a domain rule violation error
    [[nodiscard]] static Error domain_rule_violation(std::string message, std::string details = "") {
        return Error(ErrorCode::DomainRuleViolation, std::move(message), std::move(details));
    }

    /// @brief Create an infrastructure failure error
    [[nodiscard]] static Error infrastructure_failure(std::string message, std::string details = "") {
        return Error(ErrorCode::InfrastructureFailure, std::move(message), std::move(details));
    }

    /// @brief Create an internal error
    [[nodiscard]] static Error internal_error(std::string message = "Internal server error") {
        return Error(ErrorCode::InternalError, std::move(message));
    }
};

/// @brief Result type alias using std::expected
/// @tparam T Success value type
template <typename T>
using Result = std::expected<T, Error>;

/// @brief Result type for operations that don't return a value
using VoidResult = std::expected<void, Error>;

/// @brief Helper to create a success result
template <typename T>
[[nodiscard]] constexpr Result<T> ok(T&& value) {
    return Result<T>(std::forward<T>(value));
}

/// @brief Helper to create a success result for void
[[nodiscard]] constexpr VoidResult ok() {
    return VoidResult();
}

/// @brief Helper to create an error result
template <typename T>
[[nodiscard]] constexpr Result<T> err(Error error) {
    return std::unexpected(std::move(error));
}

/// @brief Helper to create a void error result
[[nodiscard]] constexpr VoidResult err_void(Error error) {
    return std::unexpected(std::move(error));
}

} // namespace pfh::application

// Domain-layer error types (no HTTP status code dependency)
namespace pfh::domain {

/// @brief Domain-specific error codes
enum class DomainErrorCode {
    // Money and currency errors
    InvalidAmount,
    InvalidCurrency,
    CurrencyMismatch,
    InvalidExchangeRate,
    ExchangeRateNotFound,

    // Account errors
    InvalidAccountType,
    AccountArchived,

    // Transaction errors
    InvalidTransactionType,
    InvalidTransferStructure,
    TransferAmountMismatch,

    // Category errors
    CategoryBoardMismatch,
    InvalidCategoryHierarchy,

    // Business rule errors
    NegativeAmount,
    ZeroAmount,
    InsufficientBalance,

    // Generic errors
    InvalidOperation,
    PreconditionFailed
};

/// @brief Domain error information
struct DomainError {
    DomainErrorCode code;
    std::string message;

    DomainError(DomainErrorCode code, std::string message)
        : code(code), message(std::move(message)) {}

    [[nodiscard]] static DomainError invalid_amount(std::string details) {
        return DomainError(DomainErrorCode::InvalidAmount, "Invalid amount: " + details);
    }

    [[nodiscard]] static DomainError currency_mismatch(std::string expected, std::string actual) {
        return DomainError(DomainErrorCode::CurrencyMismatch,
                          "Currency mismatch: expected " + expected + ", got " + actual);
    }

    [[nodiscard]] static DomainError exchange_rate_not_found(std::string from, std::string to) {
        return DomainError(DomainErrorCode::ExchangeRateNotFound,
                          "Exchange rate not found: " + from + " -> " + to);
    }
};

/// @brief Domain result type
template <typename T>
using DomainResult = std::expected<T, DomainError>;

/// @brief Domain void result type
using DomainVoidResult = std::expected<void, DomainError>;

} // namespace pfh::domain
