// Personal Finance Hub - Transaction Controller

#include "pfh/presentation/controllers/transaction_controller.h"

#include "pfh/application/input_constraints.h"
#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/json_request_parser.h"
#include "pfh/presentation/http/time_codec.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdint>
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
    if (value == "transfer") return domain::TransactionType::Transfer;
    if (value == "adjustment") return domain::TransactionType::Adjustment;
    return application::err(application::Error::validation(
        "type must be income, expense, transfer, or adjustment"));
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
    nlohmann::json tags = nlohmann::json::array();
    for (const auto& tag : value.tags) {
        tags.push_back(nlohmann::json{
            {"id", tag.id.value()},
            {"name", tag.name},
            {"isDeleted", tag.is_deleted}});
    }
    return nlohmann::json{
        {"id", value.id.value()},
        {"accountId", value.account_id.value()},
        {"type", type_text(value.type)},
        {"amount", response_amount(value)},
        {"currencyCode", value.currency_code},
        {"categoryId", value.category_id.has_value()
            ? nlohmann::json(value.category_id->value()) : nlohmann::json(nullptr)},
        {"categoryName", value.category_name.has_value()
            ? nlohmann::json(*value.category_name) : nlohmann::json(nullptr)},
        {"categoryDeleted", value.category_deleted},
        {"tags", std::move(tags)},
        {"transferGroupId", value.transfer_group_id.has_value()
            ? nlohmann::json(value.transfer_group_id->value()) : nlohmann::json(nullptr)},
        {"correctsTransactionId", value.corrects_transaction_id.has_value()
            ? nlohmann::json(value.corrects_transaction_id->value())
            : nlohmann::json(nullptr)},
        {"correctedByTransactionId", value.corrected_by_transaction_id.has_value()
            ? nlohmann::json(value.corrected_by_transaction_id->value())
            : nlohmann::json(nullptr)},
        {"description", value.description},
        {"occurredAt", TimeCodec::format_rfc3339(value.occurred_at)},
        {"createdAt", TimeCodec::format_rfc3339(value.created_at)},
        {"deletedAt", value.deleted_at.has_value()
            ? nlohmann::json(TimeCodec::format_rfc3339(*value.deleted_at))
            : nlohmann::json(nullptr)}};
}

template <typename TypedId>
[[nodiscard]] application::Result<std::optional<TypedId>> optional_query_id(
    const HttpRequest& request,
    std::string_view name) {
    const auto found = request.query.find(std::string(name));
    if (found == request.query.end()) return std::optional<TypedId>{};
    auto id = JsonRequestParser::path_id<TypedId>(found->second, name);
    if (!id) return application::err(id.error());
    return std::optional<TypedId>(*id);
}

[[nodiscard]] application::Result<std::optional<
    std::chrono::system_clock::time_point>> optional_query_time(
    const HttpRequest& request,
    std::string_view name) {
    const auto found = request.query.find(std::string(name));
    if (found == request.query.end()) {
        return std::optional<std::chrono::system_clock::time_point>{};
    }
    auto value = TimeCodec::parse_rfc3339(found->second);
    if (!value) return application::err(value.error());
    return std::optional<std::chrono::system_clock::time_point>(*value);
}

[[nodiscard]] application::Result<std::size_t> query_page_size(
    const HttpRequest& request) {
    const auto found = request.query.find("pageSize");
    if (found == request.query.end()) return application::kDefaultPageSize;
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(
        found->second.data(),
        found->second.data() + found->second.size(),
        value);
    if (found->second.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != found->second.data() + found->second.size() ||
        value == 0 || value > application::kMaximumPageSize) {
        return application::err(application::Error::validation(
            "pageSize must be between 1 and 200"));
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] application::VoidResult validate_list_query_fields(
    const HttpRequest& request) {
    static const std::set<std::string> allowed{
        "accountId", "type", "categoryId", "tagId", "from", "to",
        "keyword", "cursor", "pageSize"};
    for (const auto& [name, _] : request.query) {
        if (!allowed.contains(name)) {
            return application::err(application::Error::validation(
                "Unknown query parameter: " + name));
        }
    }
    return application::ok();
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
             "description", "occurredAt", "tagIds"});
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
    auto tag_ids = JsonRequestParser::optional_id_array<domain::TagId>(
        *body, "tagIds", domain::kMaxTagsPerTransaction);
    if (!account) return HttpResponseMapper::error(account.error(), request.trace_id);
    if (!type_value) return HttpResponseMapper::error(type_value.error(), request.trace_id);
    if (!amount) return HttpResponseMapper::error(amount.error(), request.trace_id);
    if (!currency) return HttpResponseMapper::error(currency.error(), request.trace_id);
    if (!category) return HttpResponseMapper::error(category.error(), request.trace_id);
    if (!description) return HttpResponseMapper::error(description.error(), request.trace_id);
    if (!occurred_at) return HttpResponseMapper::error(occurred_at.error(), request.trace_id);
    if (!tag_ids) return HttpResponseMapper::error(tag_ids.error(), request.trace_id);
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
        *occurred_at,
        *tag_ids}, *idempotency_key);
    return result
        ? HttpResponseMapper::json(201, transaction_json(*result))
        : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TransactionController::list(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (auto fields = validate_list_query_fields(request); !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto account = optional_query_id<domain::AccountId>(request, "accountId");
    auto category = optional_query_id<domain::CategoryId>(request, "categoryId");
    auto tag = optional_query_id<domain::TagId>(request, "tagId");
    auto from = optional_query_time(request, "from");
    auto to = optional_query_time(request, "to");
    auto page_size = query_page_size(request);
    if (!account) return HttpResponseMapper::error(account.error(), request.trace_id);
    if (!category) return HttpResponseMapper::error(category.error(), request.trace_id);
    if (!tag) return HttpResponseMapper::error(tag.error(), request.trace_id);
    if (!from) return HttpResponseMapper::error(from.error(), request.trace_id);
    if (!to) return HttpResponseMapper::error(to.error(), request.trace_id);
    if (!page_size) return HttpResponseMapper::error(page_size.error(), request.trace_id);

    std::optional<domain::TransactionType> type;
    if (const auto found = request.query.find("type");
        found != request.query.end()) {
        auto parsed = parse_type(found->second);
        if (!parsed) return HttpResponseMapper::error(parsed.error(), request.trace_id);
        type = *parsed;
    }
    application::TransactionListQuery query;
    query.user_id = *user;
    query.account_id = *account;
    query.type = type;
    query.category_id = *category;
    query.tag_id = *tag;
    query.occurred_from = *from;
    query.occurred_to = *to;
    if (const auto found = request.query.find("keyword");
        found != request.query.end()) {
        query.keyword = found->second;
    }
    query.page.page_size = *page_size;
    if (const auto found = request.query.find("cursor");
        found != request.query.end()) {
        query.page.cursor = found->second;
    }
    auto result = service_.list_transactions(query);
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    nlohmann::json items = nlohmann::json::array();
    for (const auto& transaction : result->items) {
        items.push_back(transaction_json(transaction));
    }
    return HttpResponseMapper::json(200, nlohmann::json{
        {"items", std::move(items)},
        {"nextCursor", result->next_cursor.has_value()
            ? nlohmann::json(*result->next_cursor) : nlohmann::json(nullptr)}});
}

HttpResponse TransactionController::get(
    const HttpRequest& request,
    std::string_view transaction_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TransactionId>(
        transaction_id, "transactionId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.get_transaction(*user, *id);
    return result
        ? HttpResponseMapper::json(200, transaction_json(*result))
        : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TransactionController::correct(
    const HttpRequest& request,
    std::string_view transaction_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TransactionId>(
        transaction_id, "transactionId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
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
             "description", "occurredAt", "tagIds"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto account = JsonRequestParser::required_id<domain::AccountId>(
        *body, "accountId");
    auto type_text_value = JsonRequestParser::required_string(*body, "type", 16);
    auto amount = JsonRequestParser::required_string(
        *body, "amount", application::kMaxDecimalInputLength);
    auto currency = JsonRequestParser::required_string(*body, "currencyCode", 10);
    auto category = JsonRequestParser::optional_id<domain::CategoryId>(
        *body, "categoryId");
    auto description = JsonRequestParser::optional_string_allow_empty(
        *body, "description", application::kMaxDescriptionLength);
    auto occurred_at = JsonRequestParser::optional_rfc3339(*body, "occurredAt");
    auto tag_ids = JsonRequestParser::optional_id_array<domain::TagId>(
        *body, "tagIds", domain::kMaxTagsPerTransaction);
    if (!account) return HttpResponseMapper::error(account.error(), request.trace_id);
    if (!type_text_value) return HttpResponseMapper::error(type_text_value.error(), request.trace_id);
    if (!amount) return HttpResponseMapper::error(amount.error(), request.trace_id);
    if (!currency) return HttpResponseMapper::error(currency.error(), request.trace_id);
    if (!category) return HttpResponseMapper::error(category.error(), request.trace_id);
    if (!description) return HttpResponseMapper::error(description.error(), request.trace_id);
    if (!occurred_at) return HttpResponseMapper::error(occurred_at.error(), request.trace_id);
    if (!tag_ids) return HttpResponseMapper::error(tag_ids.error(), request.trace_id);
    auto type = parse_type(*type_text_value);
    if (!type) return HttpResponseMapper::error(type.error(), request.trace_id);

    auto result = service_.correct_transaction(
        application::CorrectTransactionCommand{
            *user,
            *id,
            *account,
            *type,
            *amount,
            *currency,
            description->value_or(""),
            *category,
            *occurred_at,
            *tag_ids},
        *idempotency_key);
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
