// Personal Finance Hub - JWT Request Filter

#include "pfh/presentation/security/jwt_filter.h"

#include "pfh/application/error_mapping.h"

#include <string>

namespace pfh::presentation {

bool JwtFilter::is_public_route(
    HttpMethod method,
    std::string_view path) noexcept {
    if (method == HttpMethod::Get &&
        (path == "/livez" || path == "/readyz")) {
        return true;
    }
    if (method == HttpMethod::Post &&
        (path == "/api/v1/auth/register" ||
         path == "/api/v1/auth/login" ||
         path == "/api/v1/auth/refresh" ||
         path == "/api/v1/web/auth/register" ||
         path == "/api/v1/web/auth/login" ||
         path == "/api/v1/web/auth/refresh")) {
        return true;
    }
    return method == HttpMethod::Get &&
           (path == "/api/v1/currencies" || path == "/api/v1/timezones");
}

application::Result<RequestIdentity> JwtFilter::authenticate(
    const HttpRequest& request) {
    const auto authorization = request.header("Authorization");
    static constexpr std::string_view kBearer = "Bearer ";
    if (!authorization.has_value() ||
        !std::string_view(*authorization).starts_with(kBearer) ||
        authorization->size() <= kBearer.size()) {
        return application::err(application::Error::unauthorized(
            "Invalid or expired access token"));
    }
    const auto raw_token = std::string_view(*authorization).substr(kBearer.size());
    if (raw_token.find_first_of(" \t\r\n") != std::string_view::npos) {
        return application::err(application::Error::unauthorized(
            "Invalid or expired access token"));
    }

    const auto now = clock_.now();
    auto claims = tokens_.validate_access_token(raw_token, now);
    if (!claims) {
        return application::err(application::Error::unauthorized(
            "Invalid or expired access token"));
    }
    auto revoked = sessions_.is_access_or_session_revoked(
        claims->issuer, claims->token_id, claims->session_id, now);
    if (!revoked) {
        return application::err(application::from_repository(revoked.error()));
    }
    if (*revoked) {
        return application::err(application::Error::unauthorized(
            "Invalid or expired access token"));
    }
    auto current_role = roles_.find_role_by_id(claims->user_id);
    if (!current_role) {
        if (current_role.error().status == domain::RepositoryStatus::NotFound) {
            return application::err(application::Error::unauthorized(
                "Invalid or expired access token"));
        }
        return application::err(
            application::from_repository(current_role.error()));
    }
    if (*current_role != claims->role) {
        return application::err(application::Error::unauthorized(
            "Access role changed; refresh the session"));
    }
    return RequestIdentity{*claims};
}

} // namespace pfh::presentation
