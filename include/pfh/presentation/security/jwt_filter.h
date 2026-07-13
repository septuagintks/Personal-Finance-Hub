// Personal Finance Hub - JWT Request Filter

#pragma once

#include "pfh/application/ports/i_auth_session_repository.h"
#include "pfh/application/ports/i_clock.h"
#include "pfh/application/ports/i_token_service.h"
#include "pfh/presentation/http/http_types.h"

namespace pfh::presentation {

class JwtFilter {
public:
    JwtFilter(
        const application::ITokenService& tokens,
        application::IAuthSessionRepository& sessions,
        const application::IClock& clock)
        : tokens_(tokens), sessions_(sessions), clock_(clock) {}

    [[nodiscard]] application::Result<RequestIdentity> authenticate(
        const HttpRequest& request);

    [[nodiscard]] static bool is_public_route(
        HttpMethod method,
        std::string_view path) noexcept;

private:
    const application::ITokenService& tokens_;
    application::IAuthSessionRepository& sessions_;
    const application::IClock& clock_;
};

} // namespace pfh::presentation
