// Personal Finance Hub - OpenSSL HS256 and Opaque Token Service

#pragma once

#include "pfh/application/ports/i_token_service.h"

#ifdef PFH_HAS_POSTGRESQL

#include <chrono>
#include <string>
#include <utility>

namespace pfh::infrastructure {

class OpenSslTokenService final : public application::ITokenService {
public:
    OpenSslTokenService(
        std::string secret,
        std::string issuer,
        std::string audience,
        std::chrono::seconds access_lifetime,
        std::chrono::seconds refresh_lifetime,
        std::chrono::seconds clock_skew = std::chrono::seconds(30))
        : secret_(std::move(secret)),
          issuer_(std::move(issuer)),
          audience_(std::move(audience)),
          access_lifetime_(access_lifetime),
          refresh_lifetime_(refresh_lifetime),
          clock_skew_(clock_skew) {}
    ~OpenSslTokenService() override;

    [[nodiscard]] application::Result<application::IssuedAccessToken>
    issue_access_token(
        domain::UserId user_id,
        domain::UserRole role,
        std::string_view session_id,
        application::AuthTimePoint issued_at) const override;

    [[nodiscard]] application::Result<application::AccessTokenClaims>
    validate_access_token(
        std::string_view token,
        application::AuthTimePoint now) const override;

    [[nodiscard]] application::Result<std::string> generate_opaque_token(
        std::size_t byte_count) const override;

    [[nodiscard]] application::Result<std::string> hash_opaque_token(
        std::string_view token) const override;

    [[nodiscard]] std::chrono::seconds access_token_lifetime() const noexcept override {
        return access_lifetime_;
    }
    [[nodiscard]] std::chrono::seconds refresh_token_lifetime() const noexcept override {
        return refresh_lifetime_;
    }

private:
    std::string secret_;
    std::string issuer_;
    std::string audience_;
    std::chrono::seconds access_lifetime_;
    std::chrono::seconds refresh_lifetime_;
    std::chrono::seconds clock_skew_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
