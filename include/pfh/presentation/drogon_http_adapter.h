// Personal Finance Hub - Drogon HTTP Adapter

#pragma once

#include "pfh/infrastructure/config.h"
#include "pfh/presentation/api_application.h"

#ifdef PFH_HAS_POSTGRESQL

namespace pfh::presentation {

class DrogonHttpAdapter {
public:
    DrogonHttpAdapter(
        ApiApplication& application,
        const infrastructure::ServerConfig& server)
        : application_(application), server_(server) {}

    void configure();
    void run();

private:
    ApiApplication& application_;
    infrastructure::ServerConfig server_;
};

} // namespace pfh::presentation

#endif // PFH_HAS_POSTGRESQL
