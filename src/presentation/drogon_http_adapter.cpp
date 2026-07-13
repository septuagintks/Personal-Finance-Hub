// Personal Finance Hub - Drogon HTTP Adapter

#include "pfh/presentation/drogon_http_adapter.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/presentation/http/http_response_mapper.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <functional>
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
        response->addHeader(name, value);
    }
    return response;
}

[[nodiscard]] HttpRequest to_core(
    const drogon::HttpRequestPtr& request,
    HttpMethod method,
    std::string path) {
    HttpRequest core;
    core.method = method;
    core.path = std::move(path);
    core.body = std::string(request->body());
    const auto authorization = request->getHeader("Authorization");
    if (!authorization.empty()) {
        core.headers.emplace("Authorization", authorization);
    }
    return core;
}

} // namespace

void DrogonHttpAdapter::configure() {
    auto register_post = [this](const std::string& path) {
        drogon::app().registerHandler(
            path,
            [this, path](
                const drogon::HttpRequestPtr& request,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                callback(to_drogon(application_.handle(
                    to_core(request, HttpMethod::Post, path))));
            },
            {drogon::Post});
    };
    register_post("/api/v1/auth/register");
    register_post("/api/v1/auth/login");
    register_post("/api/v1/auth/refresh");
    register_post("/api/v1/auth/logout");

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
            callback(to_drogon(HttpResponseMapper::unexpected(trace_id)));
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
