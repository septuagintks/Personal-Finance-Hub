// Personal Finance Hub - HTTP Response and Error Mapper

#include "pfh/presentation/http/http_response_mapper.h"

namespace pfh::presentation {

HttpResponse HttpResponseMapper::json(
    int status,
    const nlohmann::json& body) {
    HttpResponse response;
    response.status = status;
    response.headers.emplace("Content-Type", "application/json; charset=utf-8");
    response.body = body.dump();
    return response;
}

HttpResponse HttpResponseMapper::no_content() {
    return HttpResponse{204, {}, {}};
}

int HttpResponseMapper::status_for(application::ErrorCode code) noexcept {
    using application::ErrorCode;
    switch (code) {
    case ErrorCode::ValidationError:
    case ErrorCode::InvalidInput:
    case ErrorCode::MissingRequiredField:
    case ErrorCode::InvalidFormat:
        return 400;
    case ErrorCode::Unauthorized:
    case ErrorCode::InvalidToken:
    case ErrorCode::ExpiredToken:
        return 401;
    case ErrorCode::Forbidden:
    case ErrorCode::InsufficientPermissions:
        return 403;
    case ErrorCode::NotFound:
    case ErrorCode::UserNotFound:
    case ErrorCode::AccountNotFound:
    case ErrorCode::TransactionNotFound:
    case ErrorCode::CategoryNotFound:
        return 404;
    case ErrorCode::Conflict:
    case ErrorCode::DuplicateResource:
    case ErrorCode::VersionMismatch:
    case ErrorCode::OptimisticLockFailure:
        return 409;
    case ErrorCode::DomainRuleViolation:
    case ErrorCode::InvalidCurrencyOperation:
    case ErrorCode::InsufficientBalance:
    case ErrorCode::TransferAmountMismatch:
    case ErrorCode::InvalidExchangeRate:
    case ErrorCode::CrossCurrencyWithoutRate:
    case ErrorCode::CategoryBoardMismatch:
    case ErrorCode::ArchivedAccountOperation:
        return 422;
    case ErrorCode::ExternalServiceError:
        return 502;
    case ErrorCode::InfrastructureFailure:
    case ErrorCode::DatabaseError:
    case ErrorCode::DatabaseConnectionFailed:
    case ErrorCode::TransactionFailed:
    case ErrorCode::ConfigurationError:
    case ErrorCode::InternalError:
    case ErrorCode::UnexpectedError:
        return 500;
    }
    return 500;
}

std::string HttpResponseMapper::code_for(application::ErrorCode code) {
    using application::ErrorCode;
    switch (code) {
    case ErrorCode::ValidationError: return "VALIDATION_ERROR";
    case ErrorCode::InvalidInput: return "INVALID_INPUT";
    case ErrorCode::MissingRequiredField: return "MISSING_REQUIRED_FIELD";
    case ErrorCode::InvalidFormat: return "INVALID_FORMAT";
    case ErrorCode::Unauthorized: return "UNAUTHORIZED";
    case ErrorCode::InvalidToken: return "INVALID_TOKEN";
    case ErrorCode::ExpiredToken: return "EXPIRED_TOKEN";
    case ErrorCode::Forbidden: return "FORBIDDEN";
    case ErrorCode::InsufficientPermissions: return "INSUFFICIENT_PERMISSIONS";
    case ErrorCode::NotFound: return "NOT_FOUND";
    case ErrorCode::UserNotFound: return "USER_NOT_FOUND";
    case ErrorCode::AccountNotFound: return "ACCOUNT_NOT_FOUND";
    case ErrorCode::TransactionNotFound: return "TRANSACTION_NOT_FOUND";
    case ErrorCode::CategoryNotFound: return "CATEGORY_NOT_FOUND";
    case ErrorCode::Conflict: return "CONFLICT";
    case ErrorCode::DuplicateResource: return "DUPLICATE_RESOURCE";
    case ErrorCode::VersionMismatch: return "VERSION_MISMATCH";
    case ErrorCode::OptimisticLockFailure: return "OPTIMISTIC_LOCK_FAILURE";
    case ErrorCode::DomainRuleViolation: return "DOMAIN_RULE_VIOLATION";
    case ErrorCode::InvalidCurrencyOperation: return "INVALID_CURRENCY_OPERATION";
    case ErrorCode::InsufficientBalance: return "INSUFFICIENT_BALANCE";
    case ErrorCode::TransferAmountMismatch: return "TRANSFER_AMOUNT_MISMATCH";
    case ErrorCode::InvalidExchangeRate: return "INVALID_EXCHANGE_RATE";
    case ErrorCode::CrossCurrencyWithoutRate: return "CROSS_CURRENCY_WITHOUT_RATE";
    case ErrorCode::CategoryBoardMismatch: return "CATEGORY_BOARD_MISMATCH";
    case ErrorCode::ArchivedAccountOperation: return "ARCHIVED_ACCOUNT_OPERATION";
    case ErrorCode::ExternalServiceError: return "EXTERNAL_SERVICE_ERROR";
    case ErrorCode::InfrastructureFailure: return "INFRASTRUCTURE_FAILURE";
    case ErrorCode::DatabaseError: return "DATABASE_ERROR";
    case ErrorCode::DatabaseConnectionFailed: return "DATABASE_CONNECTION_FAILED";
    case ErrorCode::TransactionFailed: return "TRANSACTION_FAILED";
    case ErrorCode::ConfigurationError: return "CONFIGURATION_ERROR";
    case ErrorCode::InternalError: return "INTERNAL_ERROR";
    case ErrorCode::UnexpectedError: return "UNEXPECTED_ERROR";
    }
    return "INTERNAL_ERROR";
}

HttpResponse HttpResponseMapper::error(
    const application::Error& error_value,
    std::string_view trace_id) {
    const auto status = status_for(error_value.code);
    std::string message = error_value.message;
    if (status >= 500) {
        message = status == 502 ? "An external service is unavailable"
                                : "An unexpected error occurred";
    } else if (status == 401) {
        message = "Invalid or expired access token";
    }
    nlohmann::json fields = nlohmann::json::array();
    for (const auto& field : error_value.field_errors) {
        fields.push_back(nlohmann::json{
            {"field", field.field},
            {"code", field.code},
            {"message", field.message}});
    }
    const bool retryable = error_value.retryable ||
        error_value.code == application::ErrorCode::DatabaseConnectionFailed ||
        error_value.code == application::ErrorCode::TransactionFailed ||
        error_value.code == application::ErrorCode::ExternalServiceError;
    return json(status, nlohmann::json{
        {"error_code", code_for(error_value.code)},
        {"message", message},
        {"trace_id", trace_id},
        {"retryable", retryable},
        {"field_errors", std::move(fields)}});
}

HttpResponse HttpResponseMapper::not_found(std::string_view trace_id) {
    return error(
        application::Error(application::ErrorCode::NotFound, "Route not found"),
        trace_id);
}

HttpResponse HttpResponseMapper::unexpected(std::string_view trace_id) {
    return error(application::Error::internal_error(), trace_id);
}

} // namespace pfh::presentation
