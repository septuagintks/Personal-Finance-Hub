// Personal Finance Hub - Authentication Application Service

#pragma once

#include "pfh/application/error.h"
#include "pfh/application/persistence/i_unit_of_work_factory.h"
#include "pfh/application/ports/i_auth_session_repository.h"
#include "pfh/application/ports/i_clock.h"
#include "pfh/application/ports/i_password_hasher.h"
#include "pfh/application/ports/i_registration_defaults_repository.h"
#include "pfh/application/ports/i_token_service.h"
#include "pfh/application/ports/i_user_credential_reader.h"
#include "pfh/application/security/auth_models.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"
#include "pfh/domain/repositories/i_user_repository.h"

#include <chrono>
#include <string>
#include <string_view>

namespace pfh::application {

class AuthService {
public:
    AuthService(
        domain::IUserRepository& users,
        IUserCredentialReader& credentials,
        IRegistrationDefaultsRepository& registration_defaults,
        IAuthSessionRepository& sessions,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWorkFactory& uow_factory,
        const IPasswordHasher& password_hasher,
        const ITokenService& tokens,
        const IClock& clock)
        : users_(users),
          credentials_(credentials),
          registration_defaults_(registration_defaults),
          sessions_(sessions),
          audit_logs_(audit_logs),
          uow_factory_(uow_factory),
          password_hasher_(password_hasher),
          tokens_(tokens),
          clock_(clock) {}

    [[nodiscard]] Result<RegisterResultDto> register_user(
        const RegisterCommand& command);
    [[nodiscard]] Result<TokenPairDto> login(const LoginCommand& command);
    [[nodiscard]] Result<TokenPairDto> refresh(const RefreshCommand& command);
    [[nodiscard]] VoidResult logout(const LogoutCommand& command);
    [[nodiscard]] std::chrono::seconds refresh_token_lifetime() const noexcept {
        return tokens_.refresh_token_lifetime();
    }

    [[nodiscard]] static Result<std::string> normalize_username(
        std::string_view username);

private:
    struct PendingTokens {
        std::string session_id;
        std::string refresh_token;
        std::string refresh_hash;
    };

    [[nodiscard]] Result<PendingTokens> create_pending_tokens() const;
    [[nodiscard]] Result<TokenPairDto> build_token_pair(
        domain::UserId user_id,
        domain::UserRole role,
        const PendingTokens& pending,
        AuthTimePoint now,
        IssuedAccessToken* issued = nullptr) const;
    [[nodiscard]] RefreshTokenRecord make_refresh_record(
        domain::UserId user_id,
        const PendingTokens& pending,
        AuthTimePoint now) const;

    domain::IUserRepository& users_;
    IUserCredentialReader& credentials_;
    IRegistrationDefaultsRepository& registration_defaults_;
    IAuthSessionRepository& sessions_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWorkFactory& uow_factory_;
    const IPasswordHasher& password_hasher_;
    const ITokenService& tokens_;
    const IClock& clock_;
};

} // namespace pfh::application
