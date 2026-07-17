// Personal Finance Hub - Transfer Controller

#include "pfh/presentation/controllers/transfer_controller.h"

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

[[nodiscard]] application::Result<application::TransferInputMode> parse_mode(
    const std::string& value) {
    if (value == "OutgoingAndRate") {
        return application::TransferInputMode::OutgoingAndRate;
    }
    if (value == "BothAmounts") {
        return application::TransferInputMode::BothAmounts;
    }
    if (value == "IncomingAndRate") {
        return application::TransferInputMode::IncomingAndRate;
    }
    return application::err(application::Error::validation(
        "mode must be OutgoingAndRate, BothAmounts, or IncomingAndRate"));
}

[[nodiscard]] application::Result<std::optional<domain::FeeSource>>
parse_fee_source(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return std::optional<domain::FeeSource>{};
    }
    if (*value == "SourceAccount") {
        return std::optional<domain::FeeSource>(domain::FeeSource::SourceAccount);
    }
    if (*value == "TargetAccount") {
        return std::optional<domain::FeeSource>(domain::FeeSource::TargetAccount);
    }
    if (*value == "ThirdParty") {
        return std::optional<domain::FeeSource>(domain::FeeSource::ThirdParty);
    }
    return application::err(application::Error::validation(
        "feeSource must be SourceAccount, TargetAccount, or ThirdParty"));
}

[[nodiscard]] std::string mode_text(application::TransferInputMode value) {
    switch (value) {
    case application::TransferInputMode::OutgoingAndRate:
        return "OutgoingAndRate";
    case application::TransferInputMode::BothAmounts:
        return "BothAmounts";
    case application::TransferInputMode::IncomingAndRate:
        return "IncomingAndRate";
    }
    return "BothAmounts";
}

[[nodiscard]] std::string fee_source_text(domain::FeeSource value) {
    switch (value) {
    case domain::FeeSource::SourceAccount: return "SourceAccount";
    case domain::FeeSource::TargetAccount: return "TargetAccount";
    case domain::FeeSource::ThirdParty: return "ThirdParty";
    }
    return "ThirdParty";
}

[[nodiscard]] application::Result<application::CreateTransferCommand>
parse_transfer_command(const HttpRequest& request, domain::UserId user_id) {
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return application::err(body.error());
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body,
            {"sourceAccountId", "targetAccountId", "mode", "outgoingAmount",
             "incomingAmount", "rate", "feeAmount", "feeSource",
             "feeAccountId", "description", "occurredAt"});
        !fields) return application::err(fields.error());

    auto source = JsonRequestParser::required_id<domain::AccountId>(
        *body, "sourceAccountId");
    auto target = JsonRequestParser::required_id<domain::AccountId>(
        *body, "targetAccountId");
    auto mode_value = JsonRequestParser::required_string(*body, "mode", 32);
    auto outgoing = JsonRequestParser::optional_string(
        *body, "outgoingAmount", application::kMaxDecimalInputLength);
    auto incoming = JsonRequestParser::optional_string(
        *body, "incomingAmount", application::kMaxDecimalInputLength);
    auto rate = JsonRequestParser::optional_string(
        *body, "rate", application::kMaxDecimalInputLength);
    auto fee = JsonRequestParser::optional_string(
        *body, "feeAmount", application::kMaxDecimalInputLength);
    auto fee_source_value = JsonRequestParser::optional_string(
        *body, "feeSource", 32);
    auto fee_account = JsonRequestParser::optional_id<domain::AccountId>(
        *body, "feeAccountId");
    auto description = JsonRequestParser::optional_string_allow_empty(
        *body, "description", application::kMaxDescriptionLength);
    auto occurred_at = JsonRequestParser::optional_rfc3339(*body, "occurredAt");
    if (!source) return application::err(source.error());
    if (!target) return application::err(target.error());
    if (!mode_value) return application::err(mode_value.error());
    if (!outgoing) return application::err(outgoing.error());
    if (!incoming) return application::err(incoming.error());
    if (!rate) return application::err(rate.error());
    if (!fee) return application::err(fee.error());
    if (!fee_source_value) return application::err(fee_source_value.error());
    if (!fee_account) return application::err(fee_account.error());
    if (!description) return application::err(description.error());
    if (!occurred_at) return application::err(occurred_at.error());
    auto mode = parse_mode(*mode_value);
    if (!mode) return application::err(mode.error());
    auto fee_source = parse_fee_source(*fee_source_value);
    if (!fee_source) return application::err(fee_source.error());
    return application::CreateTransferCommand{
        user_id,
        *source,
        *target,
        *mode,
        outgoing->value_or(""),
        incoming->value_or(""),
        rate->value_or(""),
        *fee,
        *fee_source,
        *fee_account,
        description->value_or(""),
        *occurred_at};
}

[[nodiscard]] nlohmann::json transfer_json(
    const application::TransferResultDto& value) {
    nlohmann::json adjustments = nlohmann::json::array();
    for (const auto id : value.adjustment_transaction_ids) {
        adjustments.push_back(id.value());
    }
    return nlohmann::json{
        {"transferGroupId", value.transfer_group_id.value()},
        {"mode", mode_text(value.mode)},
        {"sourceAccountId", value.source_account_id.value()},
        {"targetAccountId", value.target_account_id.value()},
        {"outgoingTransactionId", value.outgoing_transaction_id.value()},
        {"incomingTransactionId", value.incoming_transaction_id.value()},
        {"adjustmentTransactionIds", std::move(adjustments)},
        {"outgoingAmount", value.outgoing_amount},
        {"incomingAmount", value.incoming_amount},
        {"sourceCurrencyCode", value.source_currency_code},
        {"targetCurrencyCode", value.target_currency_code},
        {"rate", value.rate.has_value()
            ? nlohmann::json(*value.rate) : nlohmann::json(nullptr)},
        {"feeAmount", value.fee_amount.has_value()
            ? nlohmann::json(*value.fee_amount) : nlohmann::json(nullptr)},
        {"feeSource", value.fee_source.has_value()
            ? nlohmann::json(fee_source_text(*value.fee_source))
            : nlohmann::json(nullptr)},
        {"feeAccountId", value.fee_account_id.has_value()
            ? nlohmann::json(value.fee_account_id->value())
            : nlohmann::json(nullptr)},
        {"feeCurrencyCode", value.fee_currency_code.has_value()
            ? nlohmann::json(*value.fee_currency_code)
            : nlohmann::json(nullptr)},
        {"description", value.description},
        {"occurredAt", TimeCodec::format_rfc3339(value.occurred_at)},
        {"createdAt", TimeCodec::format_rfc3339(value.created_at)},
        {"deletedAt", value.deleted_at.has_value()
            ? nlohmann::json(TimeCodec::format_rfc3339(*value.deleted_at))
            : nlohmann::json(nullptr)},
        {"correctsTransferGroupId", value.corrects_transfer_group_id.has_value()
            ? nlohmann::json(value.corrects_transfer_group_id->value())
            : nlohmann::json(nullptr)},
        {"correctedByTransferGroupId",
            value.corrected_by_transfer_group_id.has_value()
            ? nlohmann::json(value.corrected_by_transfer_group_id->value())
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
        found->second.data(), found->second.data() + found->second.size(), value);
    if (found->second.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != found->second.data() + found->second.size() ||
        value == 0 || value > application::kMaximumPageSize) {
        return application::err(application::Error::validation(
            "pageSize must be between 1 and 200"));
    }
    return static_cast<std::size_t>(value);
}

} // namespace

HttpResponse TransferController::create(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    const auto idempotency_key = request.header("Idempotency-Key");
    if (!idempotency_key.has_value()) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "Idempotency-Key header is required"),
            request.trace_id);
    }
    auto command = parse_transfer_command(request, *user);
    if (!command) {
        return HttpResponseMapper::error(command.error(), request.trace_id);
    }
    auto result = service_.create_transfer(*command, *idempotency_key);
    return result ? HttpResponseMapper::json(201, transfer_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TransferController::list(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    static const std::set<std::string> allowed{
        "accountId", "from", "to", "cursor", "pageSize"};
    for (const auto& [name, _] : request.query) {
        if (!allowed.contains(name)) {
            return HttpResponseMapper::error(
                application::Error::validation(
                    "Unknown query parameter: " + name),
                request.trace_id);
        }
    }
    auto account = optional_query_id<domain::AccountId>(request, "accountId");
    auto from = optional_query_time(request, "from");
    auto to = optional_query_time(request, "to");
    auto page_size = query_page_size(request);
    if (!account) return HttpResponseMapper::error(account.error(), request.trace_id);
    if (!from) return HttpResponseMapper::error(from.error(), request.trace_id);
    if (!to) return HttpResponseMapper::error(to.error(), request.trace_id);
    if (!page_size) return HttpResponseMapper::error(page_size.error(), request.trace_id);
    application::TransferListQuery query;
    query.user_id = *user;
    query.account_id = *account;
    query.occurred_from = *from;
    query.occurred_to = *to;
    query.page.page_size = *page_size;
    if (const auto found = request.query.find("cursor");
        found != request.query.end()) query.page.cursor = found->second;
    auto result = service_.list_transfers(query);
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    nlohmann::json items = nlohmann::json::array();
    for (const auto& transfer : result->items) {
        items.push_back(transfer_json(transfer));
    }
    return HttpResponseMapper::json(200, nlohmann::json{
        {"items", std::move(items)},
        {"nextCursor", result->next_cursor.has_value()
            ? nlohmann::json(*result->next_cursor) : nlohmann::json(nullptr)}});
}

HttpResponse TransferController::get(
    const HttpRequest& request,
    std::string_view transfer_group_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TransferGroupId>(
        transfer_group_id, "transferGroupId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.get_transfer(*user, *id);
    return result ? HttpResponseMapper::json(200, transfer_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TransferController::correct(
    const HttpRequest& request,
    std::string_view transfer_group_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TransferGroupId>(
        transfer_group_id, "transferGroupId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    const auto idempotency_key = request.header("Idempotency-Key");
    if (!idempotency_key.has_value()) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "Idempotency-Key header is required"),
            request.trace_id);
    }
    auto replacement = parse_transfer_command(request, *user);
    if (!replacement) {
        return HttpResponseMapper::error(replacement.error(), request.trace_id);
    }
    auto result = service_.correct_transfer(
        application::CorrectTransferCommand{*id, std::move(*replacement)},
        *idempotency_key);
    return result ? HttpResponseMapper::json(201, transfer_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TransferController::remove(
    const HttpRequest& request,
    std::string_view transfer_group_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TransferGroupId>(
        transfer_group_id, "transferGroupId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.delete_transfer(application::DeleteTransferCommand{
        *user, *id, std::nullopt});
    return result ? HttpResponseMapper::no_content()
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

} // namespace pfh::presentation
