// Personal Finance Hub - Authentication Controller

#include "pfh/presentation/controllers/auth_controller.h"

#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/json_request_parser.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <string_view>

namespace pfh::presentation {

namespace {

constexpr std::string_view kRefreshCookieName = "pfh_refresh";
constexpr std::string_view kRefreshCookiePath = "/api/v1/web/auth";

[[nodiscard]] nlohmann::json token_json(
    const application::TokenPairDto& tokens) {
    return nlohmann::json{
        {"accessToken", tokens.access_token},
        {"refreshToken", tokens.refresh_token},
        {"expiresIn", tokens.expires_in_seconds},
        {"tokenType", tokens.token_type}};
}

[[nodiscard]] nlohmann::json web_token_json(
    const application::TokenPairDto& tokens) {
    return nlohmann::json{
        {"accessToken", tokens.access_token},
        {"expiresIn", tokens.expires_in_seconds},
        {"tokenType", tokens.token_type}};
}

[[nodiscard]] application::VoidResult validate_web_origin(
    const HttpRequest& request) {
    const auto origin = request.header("Origin");
    const auto host = request.header("Host");
    if (!origin.has_value() || !host.has_value() || host->empty() ||
        host->find_first_of("/\\ \t\r\n") != std::string::npos ||
        *origin != "https://" + *host) {
        return application::err(application::Error::forbidden(
            "Cross-site web session request rejected"));
    }
    const auto fetch_site = request.header("Sec-Fetch-Site");
    if (fetch_site.has_value() && *fetch_site != "same-origin") {
        return application::err(application::Error::forbidden(
            "Cross-site web session request rejected"));
    }
    return application::ok();
}

[[nodiscard]] std::string_view trim_cookie_part(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] application::Result<std::string> refresh_cookie(
    const HttpRequest& request) {
    const auto header = request.header("Cookie");
    if (!header.has_value() || header->size() > 16'384U) {
        return application::err(application::Error(
            application::ErrorCode::InvalidToken,
            "Web session cookie is missing or invalid"));
    }
    std::optional<std::string> value;
    std::string_view remaining(*header);
    while (!remaining.empty()) {
        const auto delimiter = remaining.find(';');
        auto part = trim_cookie_part(remaining.substr(0, delimiter));
        remaining = delimiter == std::string_view::npos
            ? std::string_view{}
            : remaining.substr(delimiter + 1);
        const auto equals = part.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const auto name = trim_cookie_part(part.substr(0, equals));
        const auto raw_value = part.substr(equals + 1);
        if (name != kRefreshCookieName) {
            continue;
        }
        if (value.has_value() || raw_value.empty() || raw_value.size() > 1024U) {
            return application::err(application::Error(
                application::ErrorCode::InvalidToken,
                "Web session cookie is missing or invalid"));
        }
        for (const char raw : raw_value) {
            const auto byte = static_cast<unsigned char>(raw);
            if (byte < 0x21U || byte > 0x7eU || raw == ';') {
                return application::err(application::Error(
                    application::ErrorCode::InvalidToken,
                    "Web session cookie is missing or invalid"));
            }
        }
        value = std::string(raw_value);
    }
    if (!value.has_value()) {
        return application::err(application::Error(
            application::ErrorCode::InvalidToken,
            "Web session cookie is missing or invalid"));
    }
    return *value;
}

void set_refresh_cookie(
    HttpResponse& response,
    std::string_view token,
    std::chrono::seconds lifetime) {
    response.headers.insert_or_assign(
        "Set-Cookie",
        std::string(kRefreshCookieName) + "=" + std::string(token) +
            "; Path=" + std::string(kRefreshCookiePath) +
            "; Max-Age=" + std::to_string(lifetime.count()) +
            "; HttpOnly; Secure; SameSite=Strict");
    response.headers.insert_or_assign("Cache-Control", "no-store");
}

void clear_refresh_cookie(HttpResponse& response) {
    response.headers.insert_or_assign(
        "Set-Cookie",
        std::string(kRefreshCookieName) +
            "=; Path=" + std::string(kRefreshCookiePath) +
            "; Max-Age=0; HttpOnly; Secure; SameSite=Strict");
    response.headers.insert_or_assign("Cache-Control", "no-store");
}

[[nodiscard]] application::Result<application::RegisterCommand>
parse_register_command(const HttpRequest& request) {
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return application::err(body.error());
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body,
            {"username", "password", "baseCurrency", "preferredLocale"});
        !fields) {
        return application::err(fields.error());
    }
    auto username = JsonRequestParser::required_string(*body, "username", 128);
    auto password = JsonRequestParser::required_string(*body, "password", 128);
    auto base_currency = JsonRequestParser::optional_string(
        *body, "baseCurrency", 10);
    auto locale = JsonRequestParser::optional_string(
        *body, "preferredLocale", 16);
    if (!username) return application::err(username.error());
    if (!password) return application::err(password.error());
    if (!base_currency) return application::err(base_currency.error());
    if (!locale) return application::err(locale.error());
    return application::RegisterCommand{
        *username, *password, base_currency->value_or("CNY"),
        locale->value_or("zh-CN")};
}

[[nodiscard]] application::Result<application::LoginCommand>
parse_login_command(const HttpRequest& request) {
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return application::err(body.error());
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body, {"username", "password"});
        !fields) {
        return application::err(fields.error());
    }
    auto username = JsonRequestParser::required_string(*body, "username", 128);
    auto password = JsonRequestParser::required_string(*body, "password", 128);
    if (!username) return application::err(username.error());
    if (!password) return application::err(password.error());
    return application::LoginCommand{*username, *password};
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

HttpResponse AuthController::web_register_user(const HttpRequest& request) {
    if (auto origin = validate_web_origin(request); !origin) {
        return HttpResponseMapper::error(origin.error(), request.trace_id);
    }
    auto command = parse_register_command(request);
    if (!command) {
        return HttpResponseMapper::error(command.error(), request.trace_id);
    }
    auto result = auth_.register_user(*command);
    if (!result) {
        return HttpResponseMapper::error(result.error(), request.trace_id);
    }
    auto body = web_token_json(result->tokens);
    body["userId"] = result->user_id.value();
    auto response = HttpResponseMapper::json(201, body);
    set_refresh_cookie(
        response, result->tokens.refresh_token, auth_.refresh_token_lifetime());
    return response;
}

HttpResponse AuthController::web_login(const HttpRequest& request) {
    if (auto origin = validate_web_origin(request); !origin) {
        return HttpResponseMapper::error(origin.error(), request.trace_id);
    }
    auto command = parse_login_command(request);
    if (!command) {
        return HttpResponseMapper::error(command.error(), request.trace_id);
    }
    auto result = auth_.login(*command);
    if (!result) {
        return HttpResponseMapper::error(result.error(), request.trace_id);
    }
    auto response = HttpResponseMapper::json(200, web_token_json(*result));
    set_refresh_cookie(
        response, result->refresh_token, auth_.refresh_token_lifetime());
    return response;
}

HttpResponse AuthController::web_refresh(const HttpRequest& request) {
    if (auto origin = validate_web_origin(request); !origin) {
        return HttpResponseMapper::error(origin.error(), request.trace_id);
    }
    auto token = refresh_cookie(request);
    if (!token) {
        return HttpResponseMapper::error(token.error(), request.trace_id);
    }
    auto result = auth_.refresh(application::RefreshCommand{*token});
    if (!result) {
        auto response = HttpResponseMapper::error(result.error(), request.trace_id);
        clear_refresh_cookie(response);
        return response;
    }
    auto response = HttpResponseMapper::json(200, web_token_json(*result));
    set_refresh_cookie(
        response, result->refresh_token, auth_.refresh_token_lifetime());
    return response;
}

HttpResponse AuthController::web_logout(const HttpRequest& request) {
    if (auto origin = validate_web_origin(request); !origin) {
        return HttpResponseMapper::error(origin.error(), request.trace_id);
    }
    if (!request.identity.has_value()) {
        return HttpResponseMapper::error(
            application::Error::unauthorized(), request.trace_id);
    }
    auto token = refresh_cookie(request);
    if (!token) {
        auto response = HttpResponseMapper::error(token.error(), request.trace_id);
        clear_refresh_cookie(response);
        return response;
    }
    auto result = auth_.logout(application::LogoutCommand{
        request.identity->access_claims, *token});
    if (!result) {
        auto response = HttpResponseMapper::error(result.error(), request.trace_id);
        clear_refresh_cookie(response);
        return response;
    }
    auto response = HttpResponseMapper::no_content();
    clear_refresh_cookie(response);
    return response;
}

} // namespace pfh::presentation
