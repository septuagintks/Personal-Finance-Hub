// Personal Finance Hub - Framework-Neutral HTTP Types

#pragma once

#include "pfh/application/security/auth_models.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace pfh::presentation {

enum class HttpMethod {
    Get,
    Post,
    Put,
    Delete,
    Unknown
};

struct RequestIdentity {
    application::AccessTokenClaims access_claims;
};

struct HttpRequest {
    HttpMethod method = HttpMethod::Unknown;
    std::string path;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> path_parameters;
    std::string body;
    std::string trace_id;
    std::optional<RequestIdentity> identity;

    [[nodiscard]] std::optional<std::string> header(std::string_view name) const {
        auto lower = [](std::string_view value) {
            std::string result(value);
            std::transform(result.begin(), result.end(), result.begin(), [](char raw) {
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(raw)));
            });
            return result;
        };
        const auto requested = lower(name);
        for (const auto& [key, value] : headers) {
            if (lower(key) == requested) {
                return value;
            }
        }
        return std::nullopt;
    }
};

struct HttpResponse {
    int status = 200;
    std::map<std::string, std::string> headers;
    std::string body;
};

} // namespace pfh::presentation
