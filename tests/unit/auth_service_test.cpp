// Personal Finance Hub - Authentication Service Tests

#include "pfh/application/services/auth_service.h"
#include "pfh/infrastructure/persistence/in_memory_audit_log_repository.h"
#include "pfh/infrastructure/persistence/in_memory_auth_session_repository.h"
#include "pfh/infrastructure/persistence/in_memory_registration_defaults_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include "pfh/infrastructure/persistence/in_memory_unit_of_work_factory.h"
#include "pfh/infrastructure/persistence/in_memory_user_repository.h"

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <string>

namespace pfh::test {

using namespace application;
using namespace domain;
using namespace infrastructure;

class FixedClock final : public IClock {
public:
    explicit FixedClock(AuthTimePoint value) : value_(value) {}
    [[nodiscard]] AuthTimePoint now() const override { return value_; }
    void advance(std::chrono::seconds amount) { value_ += amount; }

private:
    AuthTimePoint value_;
};

class TestPasswordHasher final : public IPasswordHasher {
public:
    [[nodiscard]] Result<std::string> hash(
        std::string_view password) const override {
        return "test-hash:" + std::string(password);
    }

    [[nodiscard]] Result<bool> verify(
        std::string_view password,
        std::string_view encoded_hash) const override {
        return encoded_hash == "test-hash:" + std::string(password);
    }
};

class TestTokenService final : public ITokenService {
public:
    [[nodiscard]] Result<IssuedAccessToken> issue_access_token(
        UserId user_id,
        UserRole role,
        std::string_view session_id,
        AuthTimePoint issued_at) const override {
        const auto token_id = "jti-" + std::to_string(++token_counter_);
        AccessTokenClaims claims;
        claims.issuer = "pfh-api";
        claims.audience = "pfh-client";
        claims.user_id = user_id;
        claims.role = role;
        claims.session_id = std::string(session_id);
        claims.token_id = token_id;
        claims.issued_at = issued_at;
        claims.not_before = issued_at;
        claims.expires_at = issued_at + access_lifetime_;
        const auto token = "access-" + std::to_string(user_id.value()) + "-" + token_id;
        issued_[token] = claims;
        return IssuedAccessToken{token, claims};
    }

    [[nodiscard]] Result<AccessTokenClaims> validate_access_token(
        std::string_view token,
        AuthTimePoint now) const override {
        const auto it = issued_.find(std::string(token));
        if (it == issued_.end() || it->second.expires_at <= now) {
            return err(Error(ErrorCode::InvalidToken, "Invalid token"));
        }
        return it->second;
    }

    [[nodiscard]] Result<std::string> generate_opaque_token(
        std::size_t byte_count) const override {
        return "opaque-" + std::to_string(byte_count) + "-" +
               std::to_string(++opaque_counter_);
    }

    [[nodiscard]] Result<std::string> hash_opaque_token(
        std::string_view token) const override {
        return "sha256:" + std::string(token);
    }

    [[nodiscard]] std::chrono::seconds access_token_lifetime() const noexcept override {
        return access_lifetime_;
    }

    [[nodiscard]] std::chrono::seconds refresh_token_lifetime() const noexcept override {
        return refresh_lifetime_;
    }

private:
    std::chrono::seconds access_lifetime_{900};
    std::chrono::seconds refresh_lifetime_{2'592'000};
    mutable int token_counter_ = 0;
    mutable int opaque_counter_ = 0;
    mutable std::map<std::string, AccessTokenClaims> issued_;
};

class FailingRegistrationDefaults final
    : public IRegistrationDefaultsRepository {
public:
    [[nodiscard]] RepositoryResult<RegistrationDefaultsResult> initialize(
        ITransactionContext&,
        UserId,
        const Currency&,
        std::string_view) override {
        return std::unexpected(RepositoryError::database(
            "injected defaults failure"));
    }
};

class AuthServiceTest : public ::testing::Test {
protected:
    AuthServiceTest()
        : uow_factory_(store_),
          users_(store_),
          defaults_(store_),
          sessions_(store_),
          audits_(store_),
          clock_(std::chrono::system_clock::from_time_t(1'720'000'000)),
          service_(
              users_,
              users_,
              defaults_,
              sessions_,
              audits_,
              uow_factory_,
              hasher_,
              tokens_,
              clock_) {}

    [[nodiscard]] RegisterResultDto register_alice() {
        RegisterCommand command;
        command.username = " Alice@Example.com ";
        command.password = "correct horse battery staple";
        command.base_currency_code = "USD";
        command.preferred_locale = "en-US";
        auto result = service_.register_user(command);
        EXPECT_TRUE(result.has_value())
            << (result ? "" : result.error().message);
        return result.value_or(RegisterResultDto{});
    }

    InMemoryStore store_;
    InMemoryUnitOfWorkFactory uow_factory_;
    InMemoryUserRepository users_;
    InMemoryRegistrationDefaultsRepository defaults_;
    InMemoryAuthSessionRepository sessions_;
    InMemoryAuditLogRepository audits_;
    TestPasswordHasher hasher_;
    TestTokenService tokens_;
    FixedClock clock_;
    AuthService service_;
};

TEST_F(AuthServiceTest, RegisterUser_CommitsIdentityDefaultsSessionAuditAndOutbox) {
    const auto result = register_alice();

    ASSERT_TRUE(result.user_id.is_valid());
    ASSERT_EQ(store_.users.size(), 1U);
    EXPECT_EQ(store_.users.at(result.user_id.value()).user.username(),
              "alice@example.com");
    EXPECT_TRUE(store_.users.at(result.user_id.value()).categories_initialized);
    EXPECT_TRUE(store_.preferences.contains(result.user_id.value()));
    EXPECT_FALSE(store_.categories.empty());
    ASSERT_EQ(store_.refresh_tokens.size(), 1U);
    EXPECT_NE(result.tokens.refresh_token.find("opaque-32-"), std::string::npos);
    ASSERT_EQ(store_.audit_logs.size(), 1U);
    EXPECT_EQ(store_.audit_logs.front().action, AuditAction::Register);
    ASSERT_EQ(store_.outbox.size(), 1U);
    EXPECT_EQ(store_.outbox.front().event_name, "UserRegistered");
}

TEST_F(AuthServiceTest, RegisterUser_WhenDuplicate_RollsBackEveryNewFact) {
    (void)register_alice();
    const auto users_before = store_.users.size();
    const auto tokens_before = store_.refresh_tokens.size();
    const auto audits_before = store_.audit_logs.size();
    const auto outbox_before = store_.outbox.size();

    RegisterCommand duplicate{
        "ALICE@example.com", "another secure password", "USD", "en-US"};
    const auto result = service_.register_user(duplicate);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::Conflict);
    EXPECT_EQ(store_.users.size(), users_before);
    EXPECT_EQ(store_.refresh_tokens.size(), tokens_before);
    EXPECT_EQ(store_.audit_logs.size(), audits_before);
    EXPECT_EQ(store_.outbox.size(), outbox_before);
}

TEST_F(AuthServiceTest, RegisterUser_WhenDefaultsFail_RollsBackCreatedUser) {
    FailingRegistrationDefaults failing_defaults;
    AuthService failing_service(
        users_,
        users_,
        failing_defaults,
        sessions_,
        audits_,
        uow_factory_,
        hasher_,
        tokens_,
        clock_);
    RegisterCommand command{
        "alice@example.com",
        "correct horse battery staple",
        "USD",
        "en-US"};

    const auto result = failing_service.register_user(command);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InfrastructureFailure);
    EXPECT_TRUE(store_.users.empty());
    EXPECT_TRUE(store_.preferences.empty());
    EXPECT_TRUE(store_.categories.empty());
    EXPECT_TRUE(store_.refresh_tokens.empty());
    EXPECT_TRUE(store_.audit_logs.empty());
    EXPECT_TRUE(store_.outbox.empty());
}

TEST_F(AuthServiceTest, Login_RejectsWrongPasswordWithoutLeakingIdentityState) {
    (void)register_alice();
    const auto result = service_.login(
        LoginCommand{"alice@example.com", "definitely incorrect"});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::Unauthorized);
    EXPECT_EQ(result.error().message, "Invalid username or password");
}

TEST_F(AuthServiceTest, Login_CreatesIndependentSessionWithAuditAndOutbox) {
    (void)register_alice();
    const auto result = service_.login(LoginCommand{
        "ALICE@EXAMPLE.COM", "correct horse battery staple"});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(store_.refresh_tokens.size(), 2U);
    EXPECT_EQ(store_.audit_logs.back().action, AuditAction::Login);
    EXPECT_EQ(store_.outbox.back().event_name, "UserLoggedIn");
}

TEST_F(AuthServiceTest, Refresh_RotatesTokenWithinTheSameSession) {
    const auto registered = register_alice();
    const auto old_raw = registered.tokens.refresh_token;
    const auto old_hash = "sha256:" + old_raw;
    const auto old_session = store_.refresh_tokens.at(old_hash).session_id;

    const auto result = service_.refresh(RefreshCommand{old_raw});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(store_.refresh_tokens.at(old_hash).revoked_at.has_value());
    const auto new_hash = "sha256:" + result->refresh_token;
    ASSERT_TRUE(store_.refresh_tokens.contains(new_hash));
    EXPECT_EQ(store_.refresh_tokens.at(new_hash).session_id, old_session);
    EXPECT_EQ(store_.audit_logs.back().action, AuditAction::TokenRefresh);
    EXPECT_EQ(store_.outbox.back().event_name, "TokenRefreshed");
}

TEST_F(AuthServiceTest, Refresh_ReusingRotatedTokenRevokesWholeSession) {
    const auto registered = register_alice();
    const auto first = service_.refresh(
        RefreshCommand{registered.tokens.refresh_token});
    ASSERT_TRUE(first.has_value());

    const auto reused = service_.refresh(
        RefreshCommand{registered.tokens.refresh_token});

    ASSERT_FALSE(reused.has_value());
    EXPECT_EQ(reused.error().code, ErrorCode::InvalidToken);
    const auto old_hash = "sha256:" + registered.tokens.refresh_token;
    const auto session_id = store_.refresh_tokens.at(old_hash).session_id;
    ASSERT_TRUE(store_.revoked_sessions.contains(session_id));
    const auto replacement_hash = "sha256:" + first->refresh_token;
    EXPECT_TRUE(store_.refresh_tokens.at(replacement_hash).revoked_at.has_value());
    EXPECT_EQ(store_.audit_logs.back().action, AuditAction::SecurityEvent);
    EXPECT_EQ(store_.outbox.back().event_name, "RefreshTokenReuseDetected");
}

TEST_F(AuthServiceTest, Logout_RevokesRefreshAndCurrentAccessTokenAtomically) {
    const auto registered = register_alice();
    auto claims = tokens_.validate_access_token(
        registered.tokens.access_token, clock_.now());
    ASSERT_TRUE(claims.has_value());

    const auto result = service_.logout(
        LogoutCommand{*claims, registered.tokens.refresh_token});

    ASSERT_TRUE(result.has_value()) << result.error().message;
    const auto refresh_hash = "sha256:" + registered.tokens.refresh_token;
    EXPECT_TRUE(store_.refresh_tokens.at(refresh_hash).revoked_at.has_value());
    const auto access_key = claims->issuer + "\n" + claims->token_id;
    EXPECT_TRUE(store_.revoked_access_tokens.contains(access_key));
    EXPECT_TRUE(store_.revoked_sessions.contains(claims->session_id));
    EXPECT_EQ(store_.audit_logs.back().action, AuditAction::Logout);
    EXPECT_EQ(store_.outbox.back().event_name, "UserLoggedOut");
}

TEST_F(AuthServiceTest, Logout_WithRotatedTokenRevokesReplacementSession) {
    const auto registered = register_alice();
    auto claims = tokens_.validate_access_token(
        registered.tokens.access_token, clock_.now());
    ASSERT_TRUE(claims.has_value());
    const auto rotated = service_.refresh(
        RefreshCommand{registered.tokens.refresh_token});
    ASSERT_TRUE(rotated.has_value()) << rotated.error().message;

    // A logout already in flight may still carry the just-rotated token. It
    // remains sufficient to identify and revoke the whole session.
    const auto logged_out = service_.logout(
        LogoutCommand{*claims, registered.tokens.refresh_token});
    ASSERT_TRUE(logged_out.has_value()) << logged_out.error().message;

    const auto replacement = service_.refresh(
        RefreshCommand{rotated->refresh_token});
    ASSERT_FALSE(replacement.has_value());
    EXPECT_EQ(replacement.error().code, ErrorCode::InvalidToken);
    EXPECT_TRUE(store_.revoked_sessions.contains(claims->session_id));
}

TEST(BootstrapTransactionTest, TenantCanOnlyBeBoundOnce) {
    InMemoryStore store;
    InMemoryUnitOfWork uow(store);
    auto result = uow.execute_bootstrap_transaction(
        [](ITenantBootstrapTransaction& tx) -> RepositoryVoidResult {
            auto first = tx.bind_tenant_once(UserId(1));
            if (!first) {
                return first;
            }
            auto second = tx.bind_tenant_once(UserId(1));
            EXPECT_FALSE(second.has_value());
            EXPECT_EQ(second.error().status, RepositoryStatus::Conflict);
            return {};
        });
    EXPECT_TRUE(result.has_value());
}

} // namespace pfh::test
