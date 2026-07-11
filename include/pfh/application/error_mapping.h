// Personal Finance Hub - Application Error Mapping
// Version: 1.0
// C++23
//
// Maps DomainError / RepositoryError to application::Error without leaking
// infrastructure details (SQL, stack, paths, secrets).

#pragma once

#include "pfh/application/error.h"
#include "pfh/domain/domain_error.h"
#include "pfh/domain/repositories/repository_error.h"
#include <utility>

namespace pfh::application {

[[nodiscard]] inline Error from_domain(const domain::DomainError& e) {
    using domain::DomainErrorCode;
    switch (e.code) {
    case DomainErrorCode::InvalidAmount:
    case DomainErrorCode::ParseError:
    case DomainErrorCode::NegativeAmount:
    case DomainErrorCode::ZeroAmount:
        return Error::validation(e.message);
    case DomainErrorCode::InvalidCurrency:
    case DomainErrorCode::CurrencyMismatch:
    case DomainErrorCode::InvalidExchangeRate:
    case DomainErrorCode::ExchangeRateNotFound:
    case DomainErrorCode::InvalidAccountType:
    case DomainErrorCode::AccountArchived:
    case DomainErrorCode::InvalidTransactionType:
    case DomainErrorCode::InvalidTransferStructure:
    case DomainErrorCode::TransferAmountMismatch:
    case DomainErrorCode::CategoryBoardMismatch:
    case DomainErrorCode::InvalidCategoryHierarchy:
    case DomainErrorCode::InsufficientBalance:
    case DomainErrorCode::InvalidOperation:
    case DomainErrorCode::PreconditionFailed:
        return Error::domain_rule_violation(e.message);
    case DomainErrorCode::Overflow:
    case DomainErrorCode::DivisionByZero:
        return Error::domain_rule_violation(e.message, "numeric");
    }
    return Error::internal_error(e.message);
}

[[nodiscard]] inline Error from_repository(const domain::RepositoryError& e) {
    using domain::RepositoryStatus;
    switch (e.status) {
    case RepositoryStatus::NotFound:
        return Error(ErrorCode::NotFound, e.message);
    case RepositoryStatus::ValidationError:
        return Error::validation(e.message);
    case RepositoryStatus::Conflict:
        return Error::conflict(e.message);
    case RepositoryStatus::DatabaseError:
        // Do not leak SQL / connection details to presentation.
        return Error::infrastructure_failure("Database operation failed");
    }
    return Error::internal_error();
}

template <typename T>
[[nodiscard]] Result<T> map_repo(domain::RepositoryResult<T> r) {
    if (r.has_value()) {
        return Result<T>(std::move(*r));
    }
    return err(from_repository(r.error()));
}

[[nodiscard]] inline VoidResult map_repo(domain::RepositoryVoidResult r) {
    if (r.has_value()) {
        return ok();
    }
    return err(from_repository(r.error()));
}

template <typename T>
[[nodiscard]] Result<T> map_domain(domain::DomainResult<T> r) {
    if (r.has_value()) {
        return Result<T>(std::move(*r));
    }
    return err(from_domain(r.error()));
}

[[nodiscard]] inline VoidResult map_domain(domain::DomainVoidResult r) {
    if (r.has_value()) {
        return ok();
    }
    return err(from_domain(r.error()));
}

} // namespace pfh::application
