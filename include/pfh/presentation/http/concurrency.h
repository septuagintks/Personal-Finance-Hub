// Personal Finance Hub - HTTP Optimistic Concurrency Helpers

#pragma once

#include "pfh/application/error.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace pfh::presentation {

[[nodiscard]] inline std::string version_etag(std::int64_t version) {
    return "\"" + std::to_string(version) + "\"";
}

[[nodiscard]] inline application::Result<std::int64_t> parse_if_match_version(
    std::string_view header) {
    if (header.size() < 3U || header.front() != '"' ||
        header.back() != '"' || header.starts_with("W/") ||
        header.find(',') != std::string_view::npos) {
        return application::err(application::Error::field_validation(
            "If-Match", "invalid_version_etag",
            "If-Match must contain one strong positive version ETag"));
    }
    const auto value = header.substr(1, header.size() - 2U);
    std::int64_t version = 0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), version);
    if (value.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() || version <= 0 ||
        value.front() == '0') {
        return application::err(application::Error::field_validation(
            "If-Match", "invalid_version_etag",
            "If-Match must contain one strong positive version ETag"));
    }
    return version;
}

} // namespace pfh::presentation
