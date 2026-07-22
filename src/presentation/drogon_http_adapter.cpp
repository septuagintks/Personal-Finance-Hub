// Personal Finance Hub - Drogon HTTP Adapter

#include "pfh/presentation/drogon_http_adapter.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/presentation/http/http_response_mapper.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace pfh::presentation {

namespace {

[[nodiscard]] drogon::HttpResponsePtr to_drogon(HttpResponse source) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(
        static_cast<drogon::HttpStatusCode>(source.status));
    response->setBody(std::move(source.body));
    for (const auto& [name, value] : source.headers) {
        if (name == "Content-Type") {
            response->setContentTypeString(value);
        } else {
            response->addHeader(name, value);
        }
    }
    return response;
}

[[nodiscard]] HttpRequest to_core(
    const drogon::HttpRequestPtr& request,
    HttpMethod method,
    bool copy_body) {
    HttpRequest core;
    core.method = method;
    core.path = request->path();
    if (copy_body) {
        core.body = std::string(request->body());
    }
    core.trace_id = ApiApplication::generate_trace_id();
    const auto authorization = request->getHeader("Authorization");
    if (!authorization.empty()) {
        core.headers.emplace("Authorization", authorization);
    }
    const auto if_none_match = request->getHeader("If-None-Match");
    if (!if_none_match.empty()) {
        core.headers.emplace("If-None-Match", if_none_match);
    }
    for (const auto* header : {
             "Host",
             "Origin",
             "X-Forwarded-Proto",
             "Sec-Fetch-Site",
             "Idempotency-Key",
             "If-Match"}) {
        const auto value = request->getHeader(header);
        if (!value.empty()) {
            core.headers.emplace(header, value);
        }
    }
    const auto cookie = request->getHeader("Cookie");
    if (!cookie.empty()) {
        core.headers.emplace("Cookie", cookie);
    } else {
        const auto refresh_cookie = request->getCookie("pfh_refresh");
        if (!refresh_cookie.empty()) {
            core.headers.emplace("Cookie", "pfh_refresh=" + refresh_cookie);
        }
    }
    for (const auto& [name, value] : request->getParameters()) {
        core.query.emplace(name, value);
    }
    return core;
}

using ResponseCallback =
    std::function<void(const drogon::HttpResponsePtr&)>;

void dispatch_request(
    ApiApplication& application,
    application::IBackgroundExecutor& request_executor,
    application::IBackgroundExecutor& auth_executor,
    AuthRateLimiter& auth_rate_limiter,
    std::size_t maximum_request_body_bytes,
    const drogon::HttpRequestPtr& drogon_request,
    HttpMethod method,
    ResponseCallback callback) {
    const auto path = drogon_request->path();
    const auto trace_id = ApiApplication::generate_trace_id();
    // Liveness must remain independent of the bounded application worker queue.
    if (path == "/livez") {
        auto request = to_core(drogon_request, method, true);
        request.trace_id = trace_id;
        callback(to_drogon(application.handle(std::move(request))));
        return;
    }

    const auto body_size = drogon_request->body().size();
    if (body_size > maximum_request_body_bytes) {
        spdlog::warn(
            "HTTP request body rejected trace_id={} bytes={}",
            trace_id,
            body_size);
        auto response = HttpResponseMapper::payload_too_large(trace_id);
        response.headers.insert_or_assign("X-Trace-Id", trace_id);
        callback(to_drogon(std::move(response)));
        return;
    }

    const bool password_auth =
        path == "/api/v1/auth/register" ||
        path == "/api/v1/auth/login" ||
        path == "/api/v1/web/auth/register" ||
        path == "/api/v1/web/auth/login";
    if (password_auth &&
        !auth_rate_limiter.allow(drogon_request->peerAddr().toIp())) {
        spdlog::warn("Authentication rate limited trace_id={}", trace_id);
        auto response = HttpResponseMapper::rate_limited(trace_id);
        response.headers.insert_or_assign("X-Trace-Id", trace_id);
        callback(to_drogon(std::move(response)));
        return;
    }

    auto request = to_core(drogon_request, method, false);
    request.trace_id = trace_id;
    auto response_callback =
        std::make_shared<ResponseCallback>(std::move(callback));
    auto& executor = password_auth ? auth_executor : request_executor;
    const bool accepted = executor.submit_weighted(
        [&application, request = std::move(request),
         queued_request = drogon_request,
         response_callback]() mutable {
            request.body = std::string(queued_request->body());
            queued_request.reset();
            (*response_callback)(to_drogon(
                application.handle(std::move(request))));
        },
        body_size);
    if (!accepted) {
        spdlog::warn("HTTP request queue is full trace_id={}", trace_id);
        auto response = HttpResponseMapper::overloaded(trace_id);
        response.headers.insert_or_assign("X-Trace-Id", trace_id);
        (*response_callback)(to_drogon(std::move(response)));
    }
}

} // namespace

void DrogonHttpAdapter::configure() {
    auto register_static = [this](
                               const std::string& path,
                               HttpMethod core_method,
                               drogon::HttpMethod method) {
        drogon::app().registerHandler(
            path,
            [this, core_method](
                const drogon::HttpRequestPtr& request,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                dispatch_request(
                    application_, request_executor_, auth_executor_,
                    auth_rate_limiter_, server_.maximum_request_body_bytes,
                    request, core_method, std::move(callback));
            },
            {method});
    };
    auto register_dynamic = [this](
                                const std::string& path,
                                HttpMethod core_method,
                                drogon::HttpMethod method) {
        drogon::app().registerHandler(
            path,
            [this, core_method](
                const drogon::HttpRequestPtr& request,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                std::string /*path_parameter*/) {
                dispatch_request(
                    application_, request_executor_, auth_executor_,
                    auth_rate_limiter_, server_.maximum_request_body_bytes,
                    request, core_method, std::move(callback));
            },
            {method});
    };

    register_static("/api/v1/auth/register", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/auth/login", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/auth/refresh", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/auth/logout", HttpMethod::Post, drogon::Post);
    register_static("/livez", HttpMethod::Get, drogon::Get);
    register_static("/readyz", HttpMethod::Get, drogon::Get);
    register_static("/api/v1/web/auth/register", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/web/auth/login", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/web/auth/refresh", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/web/auth/logout", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/currencies", HttpMethod::Get, drogon::Get);
    register_static("/api/v1/accounts", HttpMethod::Get, drogon::Get);
    register_static("/api/v1/accounts", HttpMethod::Post, drogon::Post);
    register_dynamic(
        "/api/v1/accounts/{1}/balance", HttpMethod::Get, drogon::Get);
    register_dynamic(
        "/api/v1/accounts/{1}/archive", HttpMethod::Post, drogon::Post);
    register_dynamic(
        "/api/v1/accounts/{1}/restore", HttpMethod::Post, drogon::Post);
    register_dynamic("/api/v1/accounts/{1}", HttpMethod::Get, drogon::Get);
    register_dynamic("/api/v1/accounts/{1}", HttpMethod::Put, drogon::Put);
    register_dynamic("/api/v1/accounts/{1}", HttpMethod::Delete, drogon::Delete);
    register_static("/api/v1/categories", HttpMethod::Get, drogon::Get);
    register_static("/api/v1/categories", HttpMethod::Post, drogon::Post);
    register_dynamic("/api/v1/categories/{1}", HttpMethod::Put, drogon::Put);
    register_dynamic(
        "/api/v1/categories/{1}", HttpMethod::Delete, drogon::Delete);
    register_dynamic(
        "/api/v1/categories/{1}/restore", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/tags", HttpMethod::Get, drogon::Get);
    register_static("/api/v1/tags", HttpMethod::Post, drogon::Post);
    register_dynamic("/api/v1/tags/{1}", HttpMethod::Put, drogon::Put);
    register_dynamic("/api/v1/tags/{1}", HttpMethod::Delete, drogon::Delete);
    register_dynamic("/api/v1/tags/{1}/restore", HttpMethod::Post, drogon::Post);
    register_dynamic(
        "/api/v1/transactions/{1}/tags", HttpMethod::Put, drogon::Put);
    register_static("/api/v1/transactions", HttpMethod::Get, drogon::Get);
    register_static("/api/v1/transactions", HttpMethod::Post, drogon::Post);
    register_dynamic(
        "/api/v1/transactions/{1}", HttpMethod::Get, drogon::Get);
    register_dynamic(
        "/api/v1/transactions/{1}/correction", HttpMethod::Post, drogon::Post);
    register_dynamic(
        "/api/v1/transactions/{1}", HttpMethod::Delete, drogon::Delete);
    register_static("/api/v1/transfers", HttpMethod::Get, drogon::Get);
    register_static("/api/v1/transfers", HttpMethod::Post, drogon::Post);
    register_dynamic("/api/v1/transfers/{1}", HttpMethod::Get, drogon::Get);
    register_dynamic(
        "/api/v1/transfers/{1}/correction", HttpMethod::Post, drogon::Post);
    register_dynamic(
        "/api/v1/transfers/{1}", HttpMethod::Delete, drogon::Delete);
    register_static(
        "/api/v1/reports/net-worth", HttpMethod::Get, drogon::Get);
    register_static(
        "/api/v1/reports/cash-flow", HttpMethod::Get, drogon::Get);
    register_static(
        "/api/v1/reports/dashboard-summary", HttpMethod::Get, drogon::Get);
    register_static(
        "/api/v1/reports/analysis", HttpMethod::Get, drogon::Get);
    register_static(
        "/api/v1/exports/transactions.csv", HttpMethod::Get, drogon::Get);
    register_static(
        "/api/v1/users/me/preferences", HttpMethod::Get, drogon::Get);
    register_static(
        "/api/v1/users/me/preferences", HttpMethod::Put, drogon::Put);
    register_static(
        "/api/v1/maintenance/audit-logs", HttpMethod::Get, drogon::Get);
    register_static(
        "/api/v1/maintenance/accounts/balance-cache/rebuild",
        HttpMethod::Post,
        drogon::Post);
    register_dynamic(
        "/api/v1/maintenance/accounts/{1}/balance-cache/rebuild",
        HttpMethod::Post,
        drogon::Post);
    register_static(
        "/api/v1/operations/summary", HttpMethod::Get, drogon::Get);
    register_static(
        "/api/v1/operations/metrics", HttpMethod::Get, drogon::Get);
    register_static(
        "/api/v1/operations/dead-letters", HttpMethod::Get, drogon::Get);
    register_dynamic(
        "/api/v1/operations/dead-letters/{1}/retry",
        HttpMethod::Post,
        drogon::Post);

    drogon::app().setExceptionHandler(
        [](const std::exception&,
           const drogon::HttpRequestPtr& request,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto trace_id = ApiApplication::generate_trace_id();
            spdlog::error(
                "Unhandled HTTP exception trace_id={} method={} path={}",
                trace_id,
                request->methodString(),
                request->path());
            auto response = HttpResponseMapper::unexpected(trace_id);
            response.headers.insert_or_assign("X-Trace-Id", trace_id);
            callback(to_drogon(std::move(response)));
        });

    drogon::app()
        .addListener(server_.host, server_.port)
        .setThreadNum(server_.threads)
        .setClientMaxBodySize(server_.maximum_request_body_bytes);
}

void DrogonHttpAdapter::run() {
    drogon::app().run();
}

} // namespace pfh::presentation

#endif // PFH_HAS_POSTGRESQL
