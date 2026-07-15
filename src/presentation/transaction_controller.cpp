// Personal Finance Hub - Transaction Controller

#include "pfh/presentation/controllers/transaction_controller.h"

#include "pfh/application/input_constraints.h"
#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/json_request_parser.h"
#include "pfh/presentation/http/time_codec.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace pfh::presentation {

namespace {

[[nodiscard]] application::Result<domain::UserId> require_user(
    const HttpRequest& request) {
    if (!request.identity.has_value() ||
        !request.identity->access_claims.user_id.is_valid()) {
        return application::err(application::Error::unauthorized());
    }
    return request.identity->access_claims.user_id;
}

[[nodiscard]] application::Result<domain::TransactionType> parse_type(
    const std::string& value) {
    if (value == "income") return domain::TransactionType::Income;
    if (value == "expense") return domain::TransactionType::Expense;
    if (value == "adjustment") return domain::TransactionType::Adjustment;
    return application::err(application::Error::validation(
        "type must be income, expense, or adjustment"));
}

[[nodiscard]] std::string type_text(domain::TransactionType value) {
    switch (value) {
    case domain::TransactionType::Income: return "income";
    case domain::TransactionType::Expense: return "expense";
    case domain::TransactionType::Transfer: return "transfer";
    case domain::TransactionType::Adjustment: return "adjustment";
    }
    return "adjustment";
}

[[nodiscard]] std::string response_amount(
    const application::TransactionDto& value) {
    if (value.type == domain::TransactionType::Expense &&
        !value.amount.empty() && value.amount.front() == '-') {
        return value.amount.substr(1);
    }
    return value.amount;
}

[[nodiscard]] nlohmann::json transaction_json(
    const application::TransactionDto& value) {
    return nlohmann::json{
        {"id", value.id.value()},
        {"accountId", value.account_id.value()},
        {"type", type_text(value.type)},
        {"amount", response_amount(value)},
        {"currencyCode", value.currency_code},
        {"categoryId", value.category_id.has_value()
            ? nlohmann::json(value.category_id->value()) : nlohmann::json(nullptr)},
        {"description", value.description},
        {"occurredAt", TimeCodec::format_rfc3339(value.occurred_at)}};
}

} // namespace

HttpResponse TransactionController::create(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    const auto idempotency_key = request.header("Idempotency-Key");
    if (!idempotency_key.has_value()) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "Idempotency-Key header is required"),
            request.trace_id);
    }
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body,
            {"accountId", "type", "amount", "currencyCode", "categoryId",
             "description", "occurredAt"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto account = JsonRequestParser::required_id<domain::AccountId>(
        *body, "accountId");
    auto type_value = JsonRequestParser::required_string(*body, "type", 16);
    auto amount = JsonRequestParser::required_string(
        *body, "amount", application::kMaxDecimalInputLength);
    auto currency = JsonRequestParser::required_string(*body, "currencyCode", 10);
    auto category = JsonRequestParser::optional_id<domain::CategoryId>(
        *body, "categoryId");
    auto description = JsonRequestParser::optional_string_allow_empty(
        *body, "description", application::kMaxDescriptionLength);
    auto occurred_at = JsonRequestParser::optional_rfc3339(*body, "occurredAt");
    if (!account) return HttpResponseMapper::error(account.error(), request.trace_id);
    if (!type_value) return HttpResponseMapper::error(type_value.error(), request.trace_id);
    if (!amount) return HttpResponseMapper::error(amount.error(), request.trace_id);
    if (!currency) return HttpResponseMapper::error(currency.error(), request.trace_id);
    if (!category) return HttpResponseMapper::error(category.error(), request.trace_id);
    if (!description) return HttpResponseMapper::error(description.error(), request.trace_id);
    if (!occurred_at) return HttpResponseMapper::error(occurred_at.error(), request.trace_id);
    auto type = parse_type(*type_value);
    if (!type) return HttpResponseMapper::error(type.error(), request.trace_id);

    auto result = service_.create_transaction(application::CreateTransactionCommand{
        *user,
        *account,
        *type,
        *amount,
        *currency,
        description->value_or(""),
        *category,
        *occurred_at}, *idempotency_key);
    return result
        ? HttpResponseMapper::json(201, transaction_json(*result))
        : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TransactionController::remove(
    const HttpRequest& request,
    std::string_view transaction_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TransactionId>(
        transaction_id, "transactionId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.delete_transaction(application::DeleteTransactionCommand{
        *user, *id, std::nullopt});
    return result ? HttpResponseMapper::no_content()
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

} // namespace pfh::presentation
