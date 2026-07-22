// Personal Finance Hub - Drogon HTTP Adapter

#pragma once

#include "pfh/application/scheduler/i_background_executor.h"
#include "pfh/presentation/api_application.h"
#include "pfh/presentation/http/auth_rate_limiter.h"

#ifdef PFH_HAS_POSTGRESQL

#include <cstdint>
#include <cstddef>
#include <chrono>
#include <string>
#include <utility>

namespace pfh::presentation {

struct HttpServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    std::uint32_t threads = 4;
    std::size_t maximum_request_body_bytes = 1024U * 1024U;
    std::uint32_t auth_rate_limit_attempts = 20;
    std::uint32_t auth_rate_limit_window_seconds = 60;
    std::size_t auth_rate_limit_sources = 10'000;
};

class DrogonHttpAdapter {
public:
    DrogonHttpAdapter(
        ApiApplication& application,
        application::IBackgroundExecutor& request_executor,
        application::IBackgroundExecutor& auth_executor,
        HttpServerConfig server)
        : application_(application),
          request_executor_(request_executor),
          auth_executor_(auth_executor),
          server_(std::move(server)),
          auth_rate_limiter_(
              server_.auth_rate_limit_attempts,
              std::chrono::seconds(server_.auth_rate_limit_window_seconds),
              server_.auth_rate_limit_sources) {}

    void configure();
    void run();

private:
    ApiApplication& application_;
    application::IBackgroundExecutor& request_executor_;
    application::IBackgroundExecutor& auth_executor_;
    HttpServerConfig server_;
    AuthRateLimiter auth_rate_limiter_;
};

} // namespace pfh::presentation

#endif // PFH_HAS_POSTGRESQL
