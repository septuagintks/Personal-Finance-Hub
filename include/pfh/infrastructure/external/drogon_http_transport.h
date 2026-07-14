// Personal Finance Hub - Drogon HTTP Transport

#pragma once

#include "pfh/infrastructure/external/i_http_transport.h"

#ifdef PFH_HAS_POSTGRESQL

#include <string>
#include <utility>

namespace pfh::infrastructure {

class DrogonHttpTransport final : public IHttpTransport {
public:
    explicit DrogonHttpTransport(std::string base_url)
        : base_url_(std::move(base_url)) {}

    [[nodiscard]] HttpTransportResult get(
        std::string_view path,
        std::chrono::milliseconds timeout) override;

private:
    std::string base_url_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
