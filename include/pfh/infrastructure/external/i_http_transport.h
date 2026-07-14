// Personal Finance Hub - Framework-Neutral HTTP Transport Port

#pragma once

#include <chrono>
#include <expected>
#include <string>
#include <string_view>

namespace pfh::infrastructure {

enum class HttpTransportErrorKind {
    Network,
    Timeout
};

struct HttpTransportError {
    HttpTransportErrorKind kind = HttpTransportErrorKind::Network;
    std::string summary;
};

struct HttpTransportResponse {
    int status_code = 0;
    std::string body;
};

using HttpTransportResult =
    std::expected<HttpTransportResponse, HttpTransportError>;

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    [[nodiscard]] virtual HttpTransportResult get(
        std::string_view path,
        std::chrono::milliseconds timeout) = 0;
};

} // namespace pfh::infrastructure
