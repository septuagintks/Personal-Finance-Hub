// Compile-only subset of Drogon's HTTP application API.
#pragma once

#include <drogon/orm/DbClient.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace drogon {

enum HttpMethod { Get, Post, Put, Delete };
enum HttpStatusCode { k200OK = 200, k500InternalServerError = 500 };

class HttpRequest {
public:
    [[nodiscard]] std::string_view body() const noexcept { return {}; }
    [[nodiscard]] std::string getHeader(const std::string&) const { return {}; }
    [[nodiscard]] std::string methodString() const { return "GET"; }
    [[nodiscard]] std::string path() const { return "/"; }
};

using HttpRequestPtr = std::shared_ptr<HttpRequest>;

class HttpResponse {
public:
    [[nodiscard]] static std::shared_ptr<HttpResponse> newHttpResponse() {
        return std::make_shared<HttpResponse>();
    }
    void setStatusCode(HttpStatusCode) {}
    void setBody(std::string) {}
    void addHeader(const std::string&, const std::string&) {}
};

using HttpResponsePtr = std::shared_ptr<HttpResponse>;

class HttpAppFramework {
public:
    template <typename Handler>
    void registerHandler(
        const std::string&,
        Handler&&,
        std::initializer_list<HttpMethod>) {}

    template <typename Handler>
    void setExceptionHandler(Handler&&) {}

    HttpAppFramework& addListener(const std::string&, std::uint16_t) {
        return *this;
    }
    HttpAppFramework& setThreadNum(std::size_t) { return *this; }
    void run() {}
};

inline HttpAppFramework& app() {
    static HttpAppFramework framework;
    return framework;
}

} // namespace drogon
