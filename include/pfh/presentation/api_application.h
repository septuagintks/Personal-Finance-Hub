// Personal Finance Hub - Framework-Neutral API Application

#pragma once

#include "pfh/presentation/controllers/auth_controller.h"
#include "pfh/presentation/http/http_types.h"
#include "pfh/presentation/security/jwt_filter.h"

#include <atomic>
#include <string>

namespace pfh::presentation {

class ApiApplication {
public:
    ApiApplication(AuthController& auth, JwtFilter& jwt_filter)
        : auth_(auth), jwt_filter_(jwt_filter) {}

    [[nodiscard]] HttpResponse handle(HttpRequest request) noexcept;
    [[nodiscard]] static std::string generate_trace_id();

private:
    AuthController& auth_;
    JwtFilter& jwt_filter_;
};

} // namespace pfh::presentation
