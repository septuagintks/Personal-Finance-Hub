// Personal Finance Hub - Authentication Controller

#include "pfh/presentation/controllers/auth_controller.h"

#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/json_request_parser.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>

namespace pfh::presentation {

namespace {

[[nodiscard]] nlohmann::json token_json(
    const application::TokenPairDto& tokens) {
    return nlohmann::json{
        {"accessToken", tokens.access_token},
        {"refreshToken", tokens.refresh_token},
        {"expiresIn", tokens.expires_in_seconds},
        {"tokenType", tokens.token_type}};
}

} // namespace

HttpResponse AuthController::register_user(const HttpRequest& request) {
    auto body = JsonRequestParser::parse_object(request);
    if (!body) {
        return HttpResponseMapper::error(body.error(), request.trace_id);
    }
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body,
            {"username", "password", "baseCurrency", "preferredLocale"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto username = JsonRequestParser::required_string(*body, "username", 128);
    auto password = JsonRequestParser::required_string(*body, "password", 128);
    auto base_currency = JsonRequestParser::optional_string(
        *body, "baseCurrency", 10);
    auto locale = JsonRequestParser::optional_string(
        *body, "preferredLocale", 16);
    if (!username) return HttpResponseMapper::error(username.error(), request.trace_id);
    if (!password) return HttpResponseMapper::error(password.error(), request.trace_id);
    if (!base_currency) return HttpResponseMapper::error(base_currency.error(), request.trace_id);
    if (!locale) return HttpResponseMapper::error(locale.error(), request.trace_id);

    application::RegisterCommand command;
    command.username = *username;
    command.password = *password;
    command.base_currency_code = base_currency->value_or("CNY");
    command.preferred_locale = locale->value_or("zh-CN");
    auto result = auth_.register_user(command);
    if (!result) {
        return HttpResponseMapper::error(result.error(), request.trace_id);
    }
    auto response = token_json(result->tokens);
    response["userId"] = result->user_id.value();
    return HttpResponseMapper::json(201, response);
}

HttpResponse AuthController::login(const HttpRequest& request) {
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body, {"username", "password"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto username = JsonRequestParser::required_string(*body, "username", 128);
    auto password = JsonRequestParser::required_string(*body, "password", 128);
    if (!username) return HttpResponseMapper::error(username.error(), request.trace_id);
    if (!password) return HttpResponseMapper::error(password.error(), request.trace_id);
    auto result = auth_.login(application::LoginCommand{*username, *password});
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    return HttpResponseMapper::json(200, token_json(*result));
}

HttpResponse AuthController::refresh(const HttpRequest& request) {
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body, {"refreshToken"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto refresh_token = JsonRequestParser::required_string(
        *body, "refreshToken", 1024);
    if (!refresh_token) {
        return HttpResponseMapper::error(refresh_token.error(), request.trace_id);
    }
    auto result = auth_.refresh(application::RefreshCommand{*refresh_token});
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    return HttpResponseMapper::json(200, token_json(*result));
}

HttpResponse AuthController::logout(const HttpRequest& request) {
    if (!request.identity.has_value()) {
        return HttpResponseMapper::error(
            application::Error::unauthorized(), request.trace_id);
    }
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body, {"refreshToken"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto refresh_token = JsonRequestParser::required_string(
        *body, "refreshToken", 1024);
    if (!refresh_token) {
        return HttpResponseMapper::error(refresh_token.error(), request.trace_id);
    }
    auto result = auth_.logout(application::LogoutCommand{
        request.identity->access_claims, *refresh_token});
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    return HttpResponseMapper::no_content();
}

} // namespace pfh::presentation
