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
    HttpMethod method) {
    HttpRequest core;
    core.method = method;
    core.path = request->path();
    core.body = std::string(request->body());
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
             "Sec-Fetch-Site",
             "Cookie",
             "Idempotency-Key",
             "If-Match"}) {
        const auto value = request->getHeader(header);
        if (!value.empty()) {
            core.headers.emplace(header, value);
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
    application::IBackgroundExecutor& executor,
    HttpRequest request,
    ResponseCallback callback) {
    const auto trace_id = request.trace_id;
    auto response_callback =
        std::make_shared<ResponseCallback>(std::move(callback));
    const bool accepted = executor.submit(
        [&application, request = std::move(request), response_callback]() mutable {
            (*response_callback)(to_drogon(
                application.handle(std::move(request))));
        });
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
                    application_, request_executor_,
                    to_core(request, core_method), std::move(callback));
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
                    application_, request_executor_,
                    to_core(request, core_method), std::move(callback));
            },
            {method});
    };

    register_static("/api/v1/auth/register", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/auth/login", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/auth/refresh", HttpMethod::Post, drogon::Post);
    register_static("/api/v1/auth/logout", HttpMethod::Post, drogon::Post);
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

    drogon::app().setExceptionHandler(
        [](const std::exception& error,
           const drogon::HttpRequestPtr& request,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto trace_id = ApiApplication::generate_trace_id();
            spdlog::error(
                "Unhandled HTTP exception trace_id={} method={} path={} error={}",
                trace_id,
                request->methodString(),
                request->path(),
                error.what());
            auto response = HttpResponseMapper::unexpected(trace_id);
            response.headers.insert_or_assign("X-Trace-Id", trace_id);
            callback(to_drogon(std::move(response)));
        });

    drogon::app()
        .addListener(server_.host, server_.port)
        .setThreadNum(server_.threads);
}

void DrogonHttpAdapter::run() {
    drogon::app().run();
}

} // namespace pfh::presentation

#endif // PFH_HAS_POSTGRESQL
