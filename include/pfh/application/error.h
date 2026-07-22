// Personal Finance Hub - Error Types and Result Handling
// Version: 1.0
// C++23
// This file defines application-layer error types and result handling

#pragma once

#include "pfh/domain/domain_error.h"
#include <expected>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

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
    ResourceLimitExceeded,

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
struct FieldError {
    std::string field;
    std::string code;
    std::string message;
};

struct Error {
    ErrorCode code;
    std::string message;
    std::string details; // Additional context for debugging
    std::vector<FieldError> field_errors;
    bool retryable = false;

    Error(
        ErrorCode code,
        std::string message,
        std::string details = "",
        std::vector<FieldError> field_errors = {},
        bool retryable = false)
        : code(code),
          message(std::move(message)),
          details(std::move(details)),
          field_errors(std::move(field_errors)),
          retryable(retryable) {}

    [[nodiscard]] static Error field_validation(
        std::string field,
        std::string code,
        std::string message) {
        const auto summary = message;
        return Error(
            ErrorCode::ValidationError,
            summary,
            {},
            {FieldError{std::move(field), std::move(code), std::move(message)}});
    }

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

/// @brief Wrap a value as a success Result. Deduces Result<decay_t<T>>.
/// Callers wanting a different target type can construct Result<T> directly.
template <typename T>
[[nodiscard]] constexpr Result<std::decay_t<T>> ok(T&& value) {
    return Result<std::decay_t<T>>(std::forward<T>(value));
}

/// @brief Success result for void.
[[nodiscard]] constexpr VoidResult ok() {
    return VoidResult();
}

/// @brief Create an error result that implicitly converts to any Result<T>
/// or VoidResult. No explicit template argument required, symmetric with ok().
///
/// Usage: return err(Error::not_found(...));  // works for Result<T> and VoidResult
[[nodiscard]] inline std::unexpected<Error> err(Error error) {
    return std::unexpected<Error>(std::move(error));
}

} // namespace pfh::application

// Note: Domain-layer error types (DomainError, DomainErrorCode, DomainResult)
// live in pfh/domain/domain_error.h so the domain layer stays free of any
// dependency on the application layer. This header includes that file above,
// so existing users of pfh::application that also referenced pfh::domain
// error types continue to compile.
