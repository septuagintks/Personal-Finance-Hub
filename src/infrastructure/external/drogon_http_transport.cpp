// Personal Finance Hub - Drogon HTTP Transport

#include "pfh/infrastructure/external/drogon_http_transport.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/HttpClient.h>

#include <chrono>
#include <exception>
#include <memory>
#include <string>

namespace pfh::infrastructure {

HttpTransportResult DrogonHttpTransport::get(
    std::string_view path,
    std::chrono::milliseconds timeout) {
    if (base_url_.empty() || path.empty() || !path.starts_with('/') ||
        timeout <= std::chrono::milliseconds::zero()) {
        return std::unexpected(HttpTransportError{
            HttpTransportErrorKind::Network,
            "HTTP transport configuration is invalid"});
    }

    try {
        const auto client = drogon::HttpClient::newHttpClient(base_url_);
        if (!client) {
            return std::unexpected(HttpTransportError{
                HttpTransportErrorKind::Network,
                "HTTP client is unavailable"});
        }
        const auto request = drogon::HttpRequest::newHttpRequest();
        request->setMethod(drogon::Get);
        request->setPath(std::string(path));
        const auto timeout_seconds =
            std::chrono::duration<double>(timeout).count();
        auto [status, response] = client->sendRequest(
            request, timeout_seconds);
        if (status != drogon::ReqResult::Ok || !response) {
            return std::unexpected(HttpTransportError{
                status == drogon::ReqResult::Timeout
                    ? HttpTransportErrorKind::Timeout
                    : HttpTransportErrorKind::Network,
                "HTTP request did not complete"});
        }
        return HttpTransportResponse{
            static_cast<int>(response->getStatusCode()),
            std::string(response->getBody())};
    } catch (const std::exception&) {
        return std::unexpected(HttpTransportError{
            HttpTransportErrorKind::Network,
            "HTTP transport raised an exception"});
    } catch (...) {
        return std::unexpected(HttpTransportError{
            HttpTransportErrorKind::Network,
            "HTTP transport raised an unknown exception"});
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
