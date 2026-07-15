// Personal Finance Hub - Transfer Controller

#include "pfh/presentation/controllers/transfer_controller.h"

#include "pfh/application/input_constraints.h"
#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/json_request_parser.h"

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

[[nodiscard]] nlohmann::json transfer_json(
    const application::TransferResultDto& value) {
    return nlohmann::json{
        {"transferGroupId", value.transfer_group_id.value()},
        {"outgoingTransactionId", value.outgoing_transaction_id.value()},
        {"incomingTransactionId", value.incoming_transaction_id.value()},
        {"outgoingAmount", value.outgoing_amount},
        {"incomingAmount", value.incoming_amount},
        {"rate", value.rate.has_value()
            ? nlohmann::json(*value.rate) : nlohmann::json(nullptr)},
        {"feeAmount", value.fee_amount.has_value()
            ? nlohmann::json(*value.fee_amount) : nlohmann::json(nullptr)}};
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
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body,
            {"sourceAccountId", "targetAccountId", "mode", "outgoingAmount",
             "incomingAmount", "rate", "feeAmount", "feeSource",
             "feeAccountId", "description", "occurredAt"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }

    auto source = JsonRequestParser::required_id<domain::AccountId>(
        *body, "sourceAccountId");
    auto target = JsonRequestParser::required_id<domain::AccountId>(
        *body, "targetAccountId");
    auto mode_text = JsonRequestParser::required_string(*body, "mode", 32);
    auto outgoing = JsonRequestParser::optional_string(
        *body, "outgoingAmount", application::kMaxDecimalInputLength);
    auto incoming = JsonRequestParser::optional_string(
        *body, "incomingAmount", application::kMaxDecimalInputLength);
    auto rate = JsonRequestParser::optional_string(
        *body, "rate", application::kMaxDecimalInputLength);
    auto fee = JsonRequestParser::optional_string(
        *body, "feeAmount", application::kMaxDecimalInputLength);
    auto fee_source_text = JsonRequestParser::optional_string(*body, "feeSource", 32);
    auto fee_account = JsonRequestParser::optional_id<domain::AccountId>(
        *body, "feeAccountId");
    auto description = JsonRequestParser::optional_string_allow_empty(
        *body, "description", application::kMaxDescriptionLength);
    auto occurred_at = JsonRequestParser::optional_rfc3339(*body, "occurredAt");
    if (!source) return HttpResponseMapper::error(source.error(), request.trace_id);
    if (!target) return HttpResponseMapper::error(target.error(), request.trace_id);
    if (!mode_text) return HttpResponseMapper::error(mode_text.error(), request.trace_id);
    if (!outgoing) return HttpResponseMapper::error(outgoing.error(), request.trace_id);
    if (!incoming) return HttpResponseMapper::error(incoming.error(), request.trace_id);
    if (!rate) return HttpResponseMapper::error(rate.error(), request.trace_id);
    if (!fee) return HttpResponseMapper::error(fee.error(), request.trace_id);
    if (!fee_source_text) {
        return HttpResponseMapper::error(fee_source_text.error(), request.trace_id);
    }
    if (!fee_account) return HttpResponseMapper::error(fee_account.error(), request.trace_id);
    if (!description) return HttpResponseMapper::error(description.error(), request.trace_id);
    if (!occurred_at) return HttpResponseMapper::error(occurred_at.error(), request.trace_id);
    auto mode = parse_mode(*mode_text);
    if (!mode) return HttpResponseMapper::error(mode.error(), request.trace_id);
    auto fee_source = parse_fee_source(*fee_source_text);
    if (!fee_source) {
        return HttpResponseMapper::error(fee_source.error(), request.trace_id);
    }

    auto result = service_.create_transfer(application::CreateTransferCommand{
        *user,
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
        *occurred_at}, *idempotency_key);
    return result ? HttpResponseMapper::json(201, transfer_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
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

} // namespace pfh::presentation
