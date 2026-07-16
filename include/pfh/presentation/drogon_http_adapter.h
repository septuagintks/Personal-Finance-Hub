// Personal Finance Hub - Drogon HTTP Adapter

#pragma once

#include "pfh/application/scheduler/i_background_executor.h"
#include "pfh/presentation/api_application.h"

#ifdef PFH_HAS_POSTGRESQL

#include <cstdint>
#include <string>
#include <utility>

namespace pfh::presentation {

struct HttpServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    std::uint32_t threads = 4;
};

class DrogonHttpAdapter {
public:
    DrogonHttpAdapter(
        ApiApplication& application,
        application::IBackgroundExecutor& request_executor,
        HttpServerConfig server)
        : application_(application),
          request_executor_(request_executor),
          server_(std::move(server)) {}

    void configure();
    void run();

private:
    ApiApplication& application_;
    application::IBackgroundExecutor& request_executor_;
    HttpServerConfig server_;
};

} // namespace pfh::presentation

#endif // PFH_HAS_POSTGRESQL
