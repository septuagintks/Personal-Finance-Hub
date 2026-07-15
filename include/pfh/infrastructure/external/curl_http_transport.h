// Personal Finance Hub - libcurl HTTPS Transport

#pragma once

#include "pfh/infrastructure/external/i_http_transport.h"

#ifdef PFH_HAS_POSTGRESQL

#include <string>
#include <utility>

namespace pfh::infrastructure {

class CurlHttpTransport final : public IHttpTransport {
public:
    explicit CurlHttpTransport(std::string base_url);

    [[nodiscard]] HttpTransportResult get(
        std::string_view path,
        const HttpQueryParameters& query,
        std::chrono::milliseconds timeout) override;

private:
    std::string base_url_;
    bool available_ = false;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
