// Personal Finance Hub - Authentication API Tests

#include "pfh/application/services/auth_service.h"
#include "pfh/infrastructure/persistence/in_memory_audit_log_repository.h"
#include "pfh/infrastructure/persistence/in_memory_auth_session_repository.h"
#include "pfh/infrastructure/persistence/in_memory_registration_defaults_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include "pfh/infrastructure/persistence/in_memory_unit_of_work_factory.h"
#include "pfh/infrastructure/persistence/in_memory_user_repository.h"
#include "pfh/presentation/api_application.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <map>
#include <string>

namespace pfh::test {

using namespace application;
using namespace domain;
using namespace infrastructure;
using namespace presentation;

class ApiFixedClock final : public IClock {
public:
    ApiFixedClock()
        : now_(std::chrono::system_clock::from_time_t(1'720'000'000)) {}
    [[nodiscard]] AuthTimePoint now() const override { return now_; }

private:
    AuthTimePoint now_;
};

class ApiPasswordHasher final : public IPasswordHasher {
public:
    [[nodiscard]] Result<std::string> hash(
        std::string_view password) const override {
        return "hash:" + std::string(password);
    }
    [[nodiscard]] Result<bool> verify(
        std::string_view password,
        std::string_view encoded_hash) const override {
        return encoded_hash == "hash:" + std::string(password);
    }
};

class ApiTokenService final : public ITokenService {
public:
    [[nodiscard]] Result<IssuedAccessToken> issue_access_token(
        UserId user_id,
        std::string_view session_id,
        AuthTimePoint issued_at) const override {
        AccessTokenClaims claims;
        claims.issuer = "pfh-api";
        claims.audience = "pfh-client";
        claims.user_id = user_id;
        claims.session_id = std::string(session_id);
        claims.token_id = "jti-" + std::to_string(++access_sequence_);
        claims.issued_at = issued_at;
        claims.not_before = issued_at;
        claims.expires_at = issued_at + access_token_lifetime();
        const auto token = "jwt-" + claims.token_id;
        claims_.insert_or_assign(token, claims);
        return IssuedAccessToken{token, claims};
    }

    [[nodiscard]] Result<AccessTokenClaims> validate_access_token(
        std::string_view token,
        AuthTimePoint now) const override {
        const auto it = claims_.find(std::string(token));
        if (it == claims_.end() || it->second.not_before > now ||
            it->second.expires_at <= now) {
            return err(Error(ErrorCode::InvalidToken, "invalid"));
        }
        return it->second;
    }

    [[nodiscard]] Result<std::string> generate_opaque_token(
        std::size_t bytes) const override {
        return "opaque-" + std::to_string(bytes) + "-" +
               std::to_string(++opaque_sequence_);
    }

    [[nodiscard]] Result<std::string> hash_opaque_token(
        std::string_view token) const override {
        return "digest:" + std::string(token);
    }

    [[nodiscard]] std::chrono::seconds access_token_lifetime() const noexcept override {
        return std::chrono::seconds(900);
    }

    [[nodiscard]] std::chrono::seconds refresh_token_lifetime() const noexcept override {
        return std::chrono::seconds(2'592'000);
    }

private:
    mutable int access_sequence_ = 0;
    mutable int opaque_sequence_ = 0;
    mutable std::map<std::string, AccessTokenClaims> claims_;
};

class AuthApiTest : public ::testing::Test {
protected:
    AuthApiTest()
        : uow_factory_(store_),
          users_(store_),
          defaults_(store_),
          sessions_(store_),
          audits_(store_),
          auth_service_(
              users_, users_, defaults_, sessions_, audits_, uow_factory_,
              hasher_, tokens_, clock_),
          auth_controller_(auth_service_),
          jwt_filter_(tokens_, sessions_, clock_),
          app_(auth_controller_, jwt_filter_) {}

    [[nodiscard]] HttpResponse post(
        std::string path,
        nlohmann::json body,
        std::string access_token = {}) {
        HttpRequest request;
        request.method = HttpMethod::Post;
        request.path = std::move(path);
        request.body = body.dump();
        if (!access_token.empty()) {
            request.headers.emplace(
                "Authorization", "Bearer " + access_token);
        }
        return app_.handle(std::move(request));
    }

    [[nodiscard]] nlohmann::json register_user() {
        const auto response = post(
            "/api/v1/auth/register",
            {{"username", "alice@example.com"},
             {"password", "correct horse battery staple"},
             {"baseCurrency", "USD"},
             {"preferredLocale", "en-US"}});
        EXPECT_EQ(response.status, 201) << response.body;
        return nlohmann::json::parse(response.body);
    }

    InMemoryStore store_;
    InMemoryUnitOfWorkFactory uow_factory_;
    InMemoryUserRepository users_;
    InMemoryRegistrationDefaultsRepository defaults_;
    InMemoryAuthSessionRepository sessions_;
    InMemoryAuditLogRepository audits_;
    ApiPasswordHasher hasher_;
    ApiTokenService tokens_;
    ApiFixedClock clock_;
    AuthService auth_service_;
    AuthController auth_controller_;
    JwtFilter jwt_filter_;
    ApiApplication app_;
};

TEST_F(AuthApiTest, Register_ReturnsCreatedTokenPairWithoutSensitiveFields) {
    const auto response = post(
        "/api/v1/auth/register",
        {{"username", "alice@example.com"},
         {"password", "correct horse battery staple"}});

    ASSERT_EQ(response.status, 201) << response.body;
    EXPECT_TRUE(response.headers.contains("X-Trace-Id"));
    const auto body = nlohmann::json::parse(response.body);
    EXPECT_TRUE(body["userId"].is_number_integer());
    EXPECT_TRUE(body["accessToken"].is_string());
    EXPECT_TRUE(body["refreshToken"].is_string());
    EXPECT_EQ(body["tokenType"], "Bearer");
    EXPECT_FALSE(body.contains("password"));
    EXPECT_FALSE(body.contains("passwordHash"));
}

TEST_F(AuthApiTest, Register_RejectsClientSuppliedUserIdAndUnknownFields) {
    const auto response = post(
        "/api/v1/auth/register",
        {{"username", "alice@example.com"},
         {"password", "correct horse battery staple"},
         {"userId", 99}});

    ASSERT_EQ(response.status, 400);
    const auto body = nlohmann::json::parse(response.body);
    EXPECT_EQ(body["error_code"], "VALIDATION_ERROR");
    EXPECT_TRUE(body["trace_id"].is_string());
}

TEST_F(AuthApiTest, Login_UsesStableUnauthorizedResponseForWrongPassword) {
    (void)register_user();
    const auto response = post(
        "/api/v1/auth/login",
        {{"username", "alice@example.com"}, {"password", "wrong password"}});

    ASSERT_EQ(response.status, 401);
    const auto body = nlohmann::json::parse(response.body);
    EXPECT_EQ(body["message"], "Invalid or expired access token");
    EXPECT_FALSE(response.body.contains("password"));
}

TEST_F(AuthApiTest, ProtectedRouteWithoutBearerTokenReturns401) {
    const auto response = post(
        "/api/v1/auth/logout", {{"refreshToken", "anything"}});
    ASSERT_EQ(response.status, 401);
    const auto body = nlohmann::json::parse(response.body);
    EXPECT_EQ(body["error_code"], "UNAUTHORIZED");
    EXPECT_TRUE(body["trace_id"].is_string());
}

TEST_F(AuthApiTest, RefreshRotatesAndReuseRevokesTheSession) {
    const auto registered = register_user();
    const auto first = post(
        "/api/v1/auth/refresh",
        {{"refreshToken", registered["refreshToken"]}});
    ASSERT_EQ(first.status, 200) << first.body;
    const auto first_body = nlohmann::json::parse(first.body);
    EXPECT_NE(first_body["refreshToken"], registered["refreshToken"]);

    const auto reused = post(
        "/api/v1/auth/refresh",
        {{"refreshToken", registered["refreshToken"]}});
    ASSERT_EQ(reused.status, 401);

    HttpRequest protected_request;
    protected_request.method = HttpMethod::Get;
    protected_request.path = "/api/v1/accounts";
    protected_request.headers.emplace(
        "Authorization", "Bearer " + first_body["accessToken"].get<std::string>());
    const auto after_reuse = app_.handle(std::move(protected_request));
    EXPECT_EQ(after_reuse.status, 401);
}

TEST_F(AuthApiTest, LogoutRevokesCurrentAccessTokenImmediately) {
    const auto registered = register_user();
    const auto access = registered["accessToken"].get<std::string>();
    const auto response = post(
        "/api/v1/auth/logout",
        {{"refreshToken", registered["refreshToken"]}},
        access);
    ASSERT_EQ(response.status, 204) << response.body;
    EXPECT_TRUE(response.body.empty());

    HttpRequest request;
    request.method = HttpMethod::Get;
    request.path = "/api/v1/accounts";
    request.headers.emplace("Authorization", "Bearer " + access);
    const auto replay = app_.handle(std::move(request));
    EXPECT_EQ(replay.status, 401);
}

TEST_F(AuthApiTest, MalformedJsonReturnsStable400WithTraceId) {
    HttpRequest request;
    request.method = HttpMethod::Post;
    request.path = "/api/v1/auth/login";
    request.body = "{not-json";
    const auto response = app_.handle(std::move(request));

    ASSERT_EQ(response.status, 400);
    const auto body = nlohmann::json::parse(response.body);
    EXPECT_EQ(body["error_code"], "VALIDATION_ERROR");
    EXPECT_TRUE(body["trace_id"].get<std::string>().starts_with("trace-"));
}

} // namespace pfh::test
