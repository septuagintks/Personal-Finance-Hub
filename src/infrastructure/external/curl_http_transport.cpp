// Personal Finance Hub - libcurl HTTPS Transport

#include "pfh/infrastructure/external/curl_http_transport.h"

#ifdef PFH_HAS_POSTGRESQL

#include <curl/curl.h>

#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace pfh::infrastructure {

namespace {

constexpr std::size_t kMaximumResponseBytes = 1024U * 1024U;
constexpr std::size_t kMaximumUrlBytes = 8192U;

struct CurlDeleter {
    void operator()(CURL* handle) const noexcept {
        if (handle != nullptr) {
            curl_easy_cleanup(handle);
        }
    }
};

class CleansedString {
public:
    ~CleansedString() {
        auto* bytes = static_cast<volatile char*>(value.data());
        for (std::size_t index = 0; index < value.size(); ++index) {
            bytes[index] = '\0';
        }
    }

    std::string value;
};

struct ResponseBuffer {
    std::string body;
    bool exceeded_limit = false;
    bool write_failed = false;
};

[[nodiscard]] bool curl_ready() noexcept {
    static const bool initialized =
        curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return initialized;
}

[[nodiscard]] bool append_escaped(
    CURL* handle,
    std::string_view value,
    std::string& output) {
    if (value.size() > static_cast<std::size_t>(INT_MAX)) {
        return false;
    }
    std::unique_ptr<char, decltype(&curl_free)> escaped(
        curl_easy_escape(
            handle,
            value.data(),
            static_cast<int>(value.size())),
        &curl_free);
    if (!escaped) {
        return false;
    }
    output.append(escaped.get());
    return output.size() <= kMaximumUrlBytes;
}

[[nodiscard]] bool build_url(
    CURL* handle,
    std::string_view base_url,
    std::string_view path,
    const HttpQueryParameters& query,
    std::string& output) {
    output.assign(base_url);
    output.append(path);
    for (std::size_t index = 0; index < query.size(); ++index) {
        const auto& [key, value] = query[index];
        if (key.empty()) {
            return false;
        }
        output.push_back(index == 0 ? '?' : '&');
        if (!append_escaped(handle, key, output)) {
            return false;
        }
        output.push_back('=');
        if (!append_escaped(handle, value, output)) {
            return false;
        }
    }
    return output.size() <= kMaximumUrlBytes;
}

std::size_t append_response(
    char* bytes,
    std::size_t size,
    std::size_t count,
    void* context) noexcept {
    auto& response = *static_cast<ResponseBuffer*>(context);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        response.exceeded_limit = true;
        return 0;
    }
    const std::size_t byte_count = size * count;
    if (response.body.size() > kMaximumResponseBytes ||
        byte_count > kMaximumResponseBytes - response.body.size()) {
        response.exceeded_limit = true;
        return 0;
    }
    try {
        response.body.append(bytes, byte_count);
    } catch (...) {
        response.write_failed = true;
        return 0;
    }
    return byte_count;
}

} // namespace

CurlHttpTransport::CurlHttpTransport(std::string base_url)
    : base_url_(std::move(base_url)), available_(curl_ready()) {}

HttpTransportResult CurlHttpTransport::get(
    std::string_view path,
    const HttpQueryParameters& query,
    std::chrono::milliseconds timeout) {
    if (!available_ || !base_url_.starts_with("https://") ||
        base_url_.size() <= 8U || base_url_.ends_with('/') ||
        base_url_.find_first_of("?#", 8U) != std::string::npos ||
        path.empty() || !path.starts_with('/') || path.contains('?') ||
        timeout <= std::chrono::milliseconds::zero() ||
        timeout.count() > static_cast<std::int64_t>(LONG_MAX)) {
        return std::unexpected(HttpTransportError{
            HttpTransportErrorKind::Network,
            "HTTPS transport configuration is invalid"});
    }

    CleansedString url;
    std::unique_ptr<CURL, CurlDeleter> handle(curl_easy_init());
    if (!handle) {
        return std::unexpected(HttpTransportError{
            HttpTransportErrorKind::Network,
            "HTTPS transport is unavailable"});
    }

    if (!build_url(handle.get(), base_url_, path, query, url.value)) {
        return std::unexpected(HttpTransportError{
            HttpTransportErrorKind::Network,
            "HTTPS request parameters are invalid"});
    }

    ResponseBuffer response;
    const long timeout_ms = static_cast<long>(timeout.count());
    const bool configured =
        curl_easy_setopt(handle.get(), CURLOPT_URL, url.value.c_str()) == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_HTTPGET, 1L) == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, timeout_ms) == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS, timeout_ms) == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "https") == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https") == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_ACCEPT_ENCODING, "") == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "pfh/0.1") == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, append_response) == CURLE_OK &&
        curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response) == CURLE_OK;
    if (!configured) {
        return std::unexpected(HttpTransportError{
            HttpTransportErrorKind::Network,
            "HTTPS request configuration failed"});
    }

    const CURLcode performed = curl_easy_perform(handle.get());
    if (performed != CURLE_OK) {
        return std::unexpected(HttpTransportError{
            performed == CURLE_OPERATION_TIMEDOUT
                ? HttpTransportErrorKind::Timeout
                : HttpTransportErrorKind::Network,
            response.exceeded_limit
                ? "HTTPS response exceeded the allowed size"
                : response.write_failed
                      ? "HTTPS response could not be buffered"
                : "HTTPS request did not complete"});
    }

    long status_code = 0;
    if (curl_easy_getinfo(
            handle.get(), CURLINFO_RESPONSE_CODE, &status_code) != CURLE_OK ||
        status_code < 0 || status_code > std::numeric_limits<int>::max()) {
        return std::unexpected(HttpTransportError{
            HttpTransportErrorKind::Network,
            "HTTPS response status is invalid"});
    }
    return HttpTransportResponse{
        static_cast<int>(status_code), std::move(response.body)};
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
