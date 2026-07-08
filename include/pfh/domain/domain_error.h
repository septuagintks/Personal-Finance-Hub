// Personal Finance Hub - Domain Error Types
// Version: 1.0
// C++23
//
// Domain-layer errors are pure business-rule errors. They do NOT depend on
// HTTP status codes, frameworks, or any outer layer. The application layer is
// responsible for mapping these to application errors / HTTP responses.

#pragma once

#include <expected>
#include <string>

namespace pfh::domain {

/// @brief Domain-specific error codes.
enum class DomainErrorCode {
    // Money and currency errors
    InvalidAmount,
    InvalidCurrency,
    CurrencyMismatch,
    InvalidExchangeRate,
    ExchangeRateNotFound,

    // Numeric errors
    Overflow,
    DivisionByZero,
    ParseError,

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

/// @brief Domain error information.
struct DomainError {
    DomainErrorCode code;
    std::string message;

    DomainError(DomainErrorCode code, std::string message)
        : code(code), message(std::move(message)) {}

    [[nodiscard]] static DomainError invalid_amount(std::string details) {
        return DomainError(DomainErrorCode::InvalidAmount, "Invalid amount: " + details);
    }

    [[nodiscard]] static DomainError parse_error(std::string details) {
        return DomainError(DomainErrorCode::ParseError, "Parse error: " + details);
    }

    [[nodiscard]] static DomainError overflow(std::string details) {
        return DomainError(DomainErrorCode::Overflow, "Arithmetic overflow: " + details);
    }

    [[nodiscard]] static DomainError division_by_zero() {
        return DomainError(DomainErrorCode::DivisionByZero, "Division by zero");
    }

    [[nodiscard]] static DomainError currency_mismatch(std::string expected, std::string actual) {
        return DomainError(DomainErrorCode::CurrencyMismatch,
                           "Currency mismatch: expected " + expected + ", got " + actual);
    }

    [[nodiscard]] static DomainError invalid_currency(std::string details) {
        return DomainError(DomainErrorCode::InvalidCurrency, "Invalid currency: " + details);
    }

    [[nodiscard]] static DomainError invalid_exchange_rate(std::string details) {
        return DomainError(DomainErrorCode::InvalidExchangeRate, "Invalid exchange rate: " + details);
    }

    [[nodiscard]] static DomainError exchange_rate_not_found(std::string from, std::string to) {
        return DomainError(DomainErrorCode::ExchangeRateNotFound,
                           "Exchange rate not found: " + from + " -> " + to);
    }

    [[nodiscard]] static DomainError invalid_operation(std::string details) {
        return DomainError(DomainErrorCode::InvalidOperation, "Invalid operation: " + details);
    }
};

/// @brief Domain result type.
template <typename T>
using DomainResult = std::expected<T, DomainError>;

/// @brief Domain void result type.
using DomainVoidResult = std::expected<void, DomainError>;

} // namespace pfh::domain
