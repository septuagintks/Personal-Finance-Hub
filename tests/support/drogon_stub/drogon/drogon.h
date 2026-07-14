// Compile-only subset of Drogon's HTTP application API.
#pragma once

#include <drogon/orm/DbClient.h>

#include <functional>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace drogon {

enum HttpMethod { Get, Post, Put, Delete };
enum HttpStatusCode { k200OK = 200, k500InternalServerError = 500 };

class HttpRequest {
public:
    [[nodiscard]] static std::shared_ptr<HttpRequest> newHttpRequest() {
        return std::make_shared<HttpRequest>();
    }
    void setMethod(HttpMethod) {}
    void setPath(std::string) {}
    [[nodiscard]] std::string_view body() const noexcept { return {}; }
    [[nodiscard]] std::string getHeader(const std::string&) const { return {}; }
    [[nodiscard]] std::string getParameter(const std::string&) const { return {}; }
    [[nodiscard]] const std::map<std::string, std::string>& getParameters() const {
        static const std::map<std::string, std::string> parameters;
        return parameters;
    }
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
    [[nodiscard]] HttpStatusCode getStatusCode() const noexcept {
        return k200OK;
    }
    [[nodiscard]] std::string getBody() const { return {}; }
};

using HttpResponsePtr = std::shared_ptr<HttpResponse>;

class EventLoop {
public:
    template <typename Callback>
    [[nodiscard]] std::uint64_t runEvery(double, Callback&&) {
        return ++next_timer_;
    }

    void invalidateTimer(std::uint64_t) {}

private:
    std::uint64_t next_timer_ = 0;
};

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
    [[nodiscard]] EventLoop* getLoop() noexcept { return &loop_; }

    template <typename Advice>
    void registerBeginningAdvice(Advice&&) {}

    template <typename Advice>
    void registerEndingAdvice(Advice&&) {}

    void quit() {}
    void run() {}

private:
    EventLoop loop_;
};

inline HttpAppFramework& app() {
    static HttpAppFramework framework;
    return framework;
}

} // namespace drogon
