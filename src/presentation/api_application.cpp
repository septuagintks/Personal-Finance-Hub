// Personal Finance Hub - Framework-Neutral API Application

#include "pfh/presentation/api_application.h"

#include "pfh/presentation/http/http_response_mapper.h"

#include <chrono>
#include <exception>
#include <string>

namespace pfh::presentation {

std::string ApiApplication::generate_trace_id() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "trace-" + std::to_string(micros) + "-" +
           std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

HttpResponse ApiApplication::handle(HttpRequest request) noexcept {
    if (request.trace_id.empty()) {
        request.trace_id = generate_trace_id();
    }
    auto finalize = [&](HttpResponse response) {
        response.headers.insert_or_assign("X-Trace-Id", request.trace_id);
        return response;
    };
    try {
        if (!JwtFilter::is_public_route(request.method, request.path)) {
            auto identity = jwt_filter_.authenticate(request);
            if (!identity) {
                return finalize(HttpResponseMapper::error(
                    identity.error(), request.trace_id));
            }
            request.identity = *identity;
        }

        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/auth/register") {
            return finalize(auth_.register_user(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/auth/login") {
            return finalize(auth_.login(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/auth/refresh") {
            return finalize(auth_.refresh(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/auth/logout") {
            return finalize(auth_.logout(request));
        }
        return finalize(HttpResponseMapper::not_found(request.trace_id));
    } catch (const std::exception&) {
        return finalize(HttpResponseMapper::unexpected(request.trace_id));
    } catch (...) {
        return finalize(HttpResponseMapper::unexpected(request.trace_id));
    }
}

} // namespace pfh::presentation
