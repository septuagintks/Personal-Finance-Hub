// Personal Finance Hub - Token Service Port

#pragma once

#include "pfh/application/error.h"
#include "pfh/application/security/auth_models.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace pfh::application {

class ITokenService {
public:
    virtual ~ITokenService() = default;

    [[nodiscard]] virtual Result<IssuedAccessToken> issue_access_token(
        domain::UserId user_id,
        domain::UserRole role,
        std::string_view session_id,
        AuthTimePoint issued_at) const = 0;

    [[nodiscard]] virtual Result<AccessTokenClaims> validate_access_token(
        std::string_view token,
        AuthTimePoint now) const = 0;

    [[nodiscard]] virtual Result<std::string> generate_opaque_token(
        std::size_t byte_count) const = 0;

    [[nodiscard]] virtual Result<std::string> hash_opaque_token(
        std::string_view token) const = 0;

    [[nodiscard]] virtual std::chrono::seconds access_token_lifetime() const noexcept = 0;
    [[nodiscard]] virtual std::chrono::seconds refresh_token_lifetime() const noexcept = 0;
};

} // namespace pfh::application
