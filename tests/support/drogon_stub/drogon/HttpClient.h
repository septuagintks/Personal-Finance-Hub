#pragma once

#include <drogon/drogon.h>

#include <string>
#include <utility>

namespace drogon {

enum class ReqResult {
    Ok,
    BadResponse,
    NetworkFailure,
    BadServerAddress,
    Timeout,
    HandshakeError
};

class HttpClient {
public:
    [[nodiscard]] static std::shared_ptr<HttpClient> newHttpClient(
        const std::string&) {
        return std::make_shared<HttpClient>();
    }

    [[nodiscard]] std::pair<ReqResult, HttpResponsePtr> sendRequest(
        const HttpRequestPtr&,
        double) {
        return {ReqResult::Ok, std::make_shared<HttpResponse>()};
    }
};

} // namespace drogon
