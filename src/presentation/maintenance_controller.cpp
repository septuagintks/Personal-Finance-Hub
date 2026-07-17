// Personal Finance Hub - Authenticated User Maintenance Controller

#include "pfh/presentation/controllers/maintenance_controller.h"

#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/json_request_parser.h"
#include "pfh/presentation/http/time_codec.h"

#include <charconv>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace pfh::presentation {

namespace {

using Json = nlohmann::json;

[[nodiscard]] application::Result<domain::UserId> require_user(
    const HttpRequest& request) {
    if (!request.identity.has_value() ||
        !request.identity->access_claims.user_id.is_valid()) {
        return application::err(application::Error::unauthorized());
    }
    return request.identity->access_claims.user_id;
}

[[nodiscard]] const char* action_text(domain::AuditAction action) noexcept {
    using domain::AuditAction;
    switch (action) {
    case AuditAction::Create: return "create";
    case AuditAction::Update: return "update";
    case AuditAction::Archive: return "archive";
    case AuditAction::Delete: return "delete";
    case AuditAction::DangerousDelete: return "dangerous_delete";
    case AuditAction::SyncImport: return "sync_import";
    case AuditAction::Refresh: return "refresh";
    case AuditAction::Register: return "register";
    case AuditAction::Login: return "login";
    case AuditAction::Logout: return "logout";
    case AuditAction::TokenRefresh: return "token_refresh";
    case AuditAction::SecurityEvent: return "security_event";
    case AuditAction::Retry: return "retry";
    }
    return "security_event";
}

[[nodiscard]] application::Result<domain::AuditAction> parse_action(
    std::string_view value) {
    using domain::AuditAction;
    if (value == "create") return AuditAction::Create;
    if (value == "update") return AuditAction::Update;
    if (value == "archive") return AuditAction::Archive;
    if (value == "delete") return AuditAction::Delete;
    if (value == "dangerous_delete") return AuditAction::DangerousDelete;
    if (value == "sync_import") return AuditAction::SyncImport;
    if (value == "refresh") return AuditAction::Refresh;
    if (value == "register") return AuditAction::Register;
    if (value == "login") return AuditAction::Login;
    if (value == "logout") return AuditAction::Logout;
    if (value == "token_refresh") return AuditAction::TokenRefresh;
    if (value == "security_event") return AuditAction::SecurityEvent;
    return application::err(application::Error::validation(
        "action is invalid"));
}

[[nodiscard]] application::Result<std::int64_t> parse_positive_integer(
    std::string_view value,
    std::string_view field) {
    std::int64_t parsed = 0;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed <= 0) {
        return application::err(application::Error::validation(
            std::string(field) + " must be a positive integer"));
    }
    return parsed;
}

[[nodiscard]] Json rebuild_json(
    const application::BalanceCacheRebuildDto& result) {
    Json accounts = Json::array();
    for (const auto& item : result.accounts) {
        accounts.push_back(Json{
            {"accountId", item.account_id.value()},
            {"currencyCode", item.currency_code},
            {"balance", item.balance},
            {"lastTransactionId", item.last_transaction_id.has_value()
                ? Json(item.last_transaction_id->value())
                : Json(nullptr)},
            {"sourceVersion", item.source_version},
            {"cacheVersion", item.cache_version},
            {"rebuiltAt", TimeCodec::format_rfc3339(item.rebuilt_at)}});
    }
    return Json{{"accounts", std::move(accounts)}};
}

} // namespace

HttpResponse MaintenanceController::list_audit_logs(
    const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);

    application::UserAuditLogQueryDto query;
    query.user_id = *user;
    if (const auto found = request.query.find("action");
        found != request.query.end()) {
        auto parsed = parse_action(found->second);
        if (!parsed) {
            return HttpResponseMapper::error(parsed.error(), request.trace_id);
        }
        query.action = *parsed;
    }
    if (const auto found = request.query.find("resourceType");
        found != request.query.end()) {
        query.resource_type = found->second;
    }
    if (const auto found = request.query.find("from");
        found != request.query.end()) {
        auto parsed = TimeCodec::parse_rfc3339(found->second);
        if (!parsed) {
            return HttpResponseMapper::error(parsed.error(), request.trace_id);
        }
        query.from = *parsed;
    }
    if (const auto found = request.query.find("to");
        found != request.query.end()) {
        auto parsed = TimeCodec::parse_rfc3339(found->second);
        if (!parsed) {
            return HttpResponseMapper::error(parsed.error(), request.trace_id);
        }
        query.to = *parsed;
    }
    if (const auto found = request.query.find("cursor");
        found != request.query.end()) {
        auto parsed = parse_positive_integer(found->second, "cursor");
        if (!parsed) {
            return HttpResponseMapper::error(parsed.error(), request.trace_id);
        }
        query.before_id = *parsed;
    }
    if (const auto found = request.query.find("pageSize");
        found != request.query.end()) {
        auto parsed = parse_positive_integer(found->second, "pageSize");
        if (!parsed || *parsed > 100) {
            return HttpResponseMapper::error(
                application::Error::validation(
                    "pageSize must be between 1 and 100"),
                request.trace_id);
        }
        query.page_size = static_cast<std::size_t>(*parsed);
    }

    auto result = service_.list_user_audit_logs(query);
    if (!result) {
        return HttpResponseMapper::error(result.error(), request.trace_id);
    }
    Json items = Json::array();
    for (const auto& item : result->items) {
        items.push_back(Json{
            {"id", item.id},
            {"action", action_text(item.action)},
            {"resourceType", item.resource_type},
            {"resourceId", item.resource_id},
            {"result", item.result},
            {"traceId", item.trace_id.has_value()
                ? Json(*item.trace_id)
                : Json(nullptr)},
            {"occurredAt", TimeCodec::format_rfc3339(item.occurred_at)}});
    }
    return HttpResponseMapper::json(200, Json{
        {"items", std::move(items)},
        {"nextCursor", result->next_cursor.has_value()
            ? Json(*result->next_cursor)
            : Json(nullptr)}});
}

HttpResponse MaintenanceController::rebuild_all_balance_caches(
    const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    auto result = service_.rebuild_balance_cache(
        application::RebuildBalanceCacheCommand{
            *user, std::nullopt, request.trace_id});
    return result
        ? HttpResponseMapper::json(200, rebuild_json(*result))
        : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse MaintenanceController::rebuild_account_balance_cache(
    const HttpRequest& request,
    std::string_view account_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::AccountId>(
        account_id, "accountId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.rebuild_balance_cache(
        application::RebuildBalanceCacheCommand{
            *user, *id, request.trace_id});
    return result
        ? HttpResponseMapper::json(200, rebuild_json(*result))
        : HttpResponseMapper::error(result.error(), request.trace_id);
}

} // namespace pfh::presentation
