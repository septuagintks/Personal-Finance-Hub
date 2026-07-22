// Personal Finance Hub - Authentication Application Models
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/user.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace pfh::application {

using AuthTimePoint = std::chrono::system_clock::time_point;

struct UserCredentialRecord {
    domain::User user;
    std::string password_hash;
    domain::Currency base_currency;
    bool categories_initialized = false;
};

struct AccessTokenClaims {
    std::string issuer;
    std::string audience;
    domain::UserId user_id;
    std::string session_id;
    std::string token_id;
    domain::UserRole role = domain::UserRole::User;
    AuthTimePoint issued_at{};
    AuthTimePoint not_before{};
    AuthTimePoint expires_at{};
};

struct IssuedAccessToken {
    std::string token;
    AccessTokenClaims claims;
};

struct RefreshTokenRecord {
    std::int64_t id = 0;
    domain::UserId user_id;
    std::string token_hash;
    std::string session_id;
    AuthTimePoint expires_at{};
    AuthTimePoint created_at{};
    std::optional<AuthTimePoint> revoked_at;
};

struct RegisterCommand {
    std::string username;
    std::string password;
    std::string base_currency_code = "CNY";
    std::string preferred_locale = "zh-CN";
    std::optional<std::string> preferred_timezone;
};

struct LoginCommand {
    std::string username;
    std::string password;
};

struct RefreshCommand {
    std::string refresh_token;
};

struct LogoutCommand {
    AccessTokenClaims access_claims;
    std::string refresh_token;
};

struct TokenPairDto {
    std::string access_token;
    std::string refresh_token;
    std::int64_t expires_in_seconds = 0;
    std::string token_type = "Bearer";
    domain::UserRole role = domain::UserRole::User;
};

struct RegisterResultDto {
    domain::UserId user_id;
    TokenPairDto tokens;
};

struct RegistrationDefaultsResult {
    std::string resolved_locale;
    std::size_t category_count = 0;
};

} // namespace pfh::application
