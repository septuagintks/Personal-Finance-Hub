// Personal Finance Hub - HTTP Response and Error Mapper

#pragma once

#include "pfh/application/error.h"
#include "pfh/presentation/http/http_types.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace pfh::presentation {

class HttpResponseMapper {
public:
    [[nodiscard]] static HttpResponse json(
        int status,
        const nlohmann::json& body);
    [[nodiscard]] static HttpResponse no_content();
    [[nodiscard]] static HttpResponse error(
        const application::Error& error,
        std::string_view trace_id);
    [[nodiscard]] static HttpResponse not_found(std::string_view trace_id);
    [[nodiscard]] static HttpResponse unexpected(std::string_view trace_id);
    [[nodiscard]] static HttpResponse overloaded(std::string_view trace_id);

    [[nodiscard]] static int status_for(application::ErrorCode code) noexcept;
    [[nodiscard]] static std::string code_for(application::ErrorCode code);
};

} // namespace pfh::presentation
