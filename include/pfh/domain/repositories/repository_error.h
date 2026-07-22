// Personal Finance Hub - Repository Error Types
// Version: 1.0
// C++23
//
// Repository errors describe data-access outcomes only. They do not carry
// framework details (SQL text, connection pool state, Drogon types).

#pragma once

#include <expected>
#include <string>
#include <utility>

namespace pfh::domain {

enum class RepositoryStatus {
    NotFound,
    ValidationError,
    ResourceLimitExceeded,
    Conflict,
    DatabaseError
};

struct RepositoryError {
    RepositoryStatus status;
    std::string message;

    RepositoryError(RepositoryStatus status, std::string message)
        : status(status), message(std::move(message)) {}

    [[nodiscard]] static RepositoryError not_found(std::string message) {
        return RepositoryError(RepositoryStatus::NotFound, std::move(message));
    }

    [[nodiscard]] static RepositoryError validation(std::string message) {
        return RepositoryError(RepositoryStatus::ValidationError, std::move(message));
    }

    [[nodiscard]] static RepositoryError resource_limit(std::string message) {
        return RepositoryError(
            RepositoryStatus::ResourceLimitExceeded, std::move(message));
    }

    [[nodiscard]] static RepositoryError conflict(std::string message) {
        return RepositoryError(RepositoryStatus::Conflict, std::move(message));
    }

    [[nodiscard]] static RepositoryError database(std::string message) {
        return RepositoryError(RepositoryStatus::DatabaseError, std::move(message));
    }
};

template <typename T>
using RepositoryResult = std::expected<T, RepositoryError>;

using RepositoryVoidResult = std::expected<void, RepositoryError>;

} // namespace pfh::domain
