// Personal Finance Hub - Authentication Application Service

#include "pfh/application/services/auth_service.h"

#include "pfh/application/error_mapping.h"
#include "pfh/application/input_constraints.h"
#include "pfh/domain/events/domain_events.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace pfh::application {

namespace {

constexpr std::size_t kMinPasswordLength = 12;
constexpr std::size_t kMaxPasswordLength = 128;
constexpr std::size_t kMaxRefreshTokenLength = 1024;

[[nodiscard]] Error invalid_credentials() {
    return Error::unauthorized("Invalid username or password");
}

[[nodiscard]] Error invalid_token() {
    return Error(ErrorCode::InvalidToken, "Invalid or expired token");
}

[[nodiscard]] domain::AuditLogEntry auth_audit(
    domain::UserId user_id,
    domain::AuditAction action,
    std::string resource_type,
    std::string resource_id,
    std::string metadata,
    AuthTimePoint occurred_at) {
    domain::AuditLogEntry entry;
    entry.operator_user_id = user_id;
    entry.action = action;
    entry.resource_type = std::move(resource_type);
    entry.resource_id = std::move(resource_id);
    entry.metadata_json = std::move(metadata);
    entry.occurred_at = occurred_at;
    return entry;
}

} // namespace

Result<std::string> AuthService::normalize_username(std::string_view username) {
    auto first = username.begin();
    auto last = username.end();
    while (first != last && std::isspace(static_cast<unsigned char>(*first)) != 0) {
        ++first;
    }
    while (first != last &&
           std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
        --last;
    }
    if (first == last) {
        return err(Error::validation("username is required"));
    }

    std::string result(first, last);
    if (result.size() < 3 || result.size() > 64) {
        return err(Error::validation("username must contain 3 to 64 characters"));
    }
    for (char& raw : result) {
        const auto c = static_cast<unsigned char>(raw);
        if (c < 0x21 || c > 0x7e) {
            return err(Error::validation(
                "username must contain printable ASCII characters without spaces"));
        }
        raw = static_cast<char>(std::tolower(c));
    }
    return result;
}

Result<AuthService::PendingTokens> AuthService::create_pending_tokens() const {
    auto session_id = tokens_.generate_opaque_token(24);
    if (!session_id) {
        return err(session_id.error());
    }
    auto refresh_token = tokens_.generate_opaque_token(32);
    if (!refresh_token) {
        return err(refresh_token.error());
    }
    auto refresh_hash = tokens_.hash_opaque_token(*refresh_token);
    if (!refresh_hash) {
        return err(refresh_hash.error());
    }
    return PendingTokens{*session_id, *refresh_token, *refresh_hash};
}

Result<TokenPairDto> AuthService::build_token_pair(
    domain::UserId user_id,
    const PendingTokens& pending,
    AuthTimePoint now,
    IssuedAccessToken* issued) const {
    auto access = tokens_.issue_access_token(user_id, pending.session_id, now);
    if (!access) {
        return err(access.error());
    }
    if (issued != nullptr) {
        *issued = *access;
    }
    TokenPairDto result;
    result.access_token = access->token;
    result.refresh_token = pending.refresh_token;
    result.expires_in_seconds = tokens_.access_token_lifetime().count();
    return result;
}

RefreshTokenRecord AuthService::make_refresh_record(
    domain::UserId user_id,
    const PendingTokens& pending,
    AuthTimePoint now) const {
    RefreshTokenRecord record;
    record.user_id = user_id;
    record.token_hash = pending.refresh_hash;
    record.session_id = pending.session_id;
    record.created_at = now;
    record.expires_at = now + tokens_.refresh_token_lifetime();
    return record;
}

Result<RegisterResultDto> AuthService::register_user(
    const RegisterCommand& command) {
    auto username = normalize_username(command.username);
    if (!username) {
        return err(username.error());
    }
    if (command.password.size() < kMinPasswordLength ||
        command.password.size() > kMaxPasswordLength) {
        return err(Error::validation(
            "password must contain 12 to 128 characters"));
    }
    if (!is_locale_tag(command.preferred_locale)) {
        return err(Error::validation("preferredLocale is invalid"));
    }
    auto currency = domain::Currency::create(command.base_currency_code);
    if (!currency) {
        return err(from_domain(currency.error()));
    }
    auto password_hash = password_hasher_.hash(command.password);
    if (!password_hash) {
        return err(password_hash.error());
    }
    auto pending = create_pending_tokens();
    if (!pending) {
        return err(pending.error());
    }

    auto uow = uow_factory_.create_bootstrap();
    if (!uow) {
        return err(Error::infrastructure_failure(
            "Registration transaction is unavailable"));
    }
    const auto now = clock_.now();
    std::optional<domain::UserId> new_user_id;
    std::optional<TokenPairDto> token_pair;
    std::optional<Error> app_error;

    auto write = uow->execute_bootstrap_transaction(
        [&](ITenantBootstrapTransaction& tx) -> domain::RepositoryVoidResult {
            auto created = users_.create(tx, *username, *password_hash, *currency);
            if (!created) {
                return std::unexpected(created.error());
            }
            new_user_id = *created;

            if (auto bound = tx.bind_tenant_once(*created); !bound) {
                return bound;
            }
            auto defaults = registration_defaults_.initialize(
                tx, *created, *currency, command.preferred_locale);
            if (!defaults) {
                return std::unexpected(defaults.error());
            }

            IssuedAccessToken issued;
            auto pair = build_token_pair(*created, *pending, now, &issued);
            if (!pair) {
                app_error = pair.error();
                return std::unexpected(domain::RepositoryError::database(
                    "Access token creation failed"));
            }
            token_pair = *pair;

            auto refresh = make_refresh_record(*created, *pending, now);
            if (auto saved = sessions_.save_refresh_token(tx, refresh); !saved) {
                return saved;
            }
            auto audit = auth_audit(
                *created,
                domain::AuditAction::Register,
                "User",
                created->to_string(),
                "{\"locale\":" +
                    domain::event_detail::json_string(
                        defaults->resolved_locale) +
                    ",\"categoryCount\":" +
                    std::to_string(defaults->category_count) + "}",
                now);
            if (auto appended = audit_logs_.append(tx, audit); !appended) {
                return appended;
            }
            uow->register_event(std::make_shared<domain::UserRegisteredEvent>(
                *created, defaults->resolved_locale, now));
            return {};
        });
    if (!write) {
        if (app_error.has_value()) {
            return err(*app_error);
        }
        return err(from_repository(write.error()));
    }
    return RegisterResultDto{*new_user_id, *token_pair};
}

Result<TokenPairDto> AuthService::login(const LoginCommand& command) {
    auto username = normalize_username(command.username);
    if (!username || command.password.empty() ||
        command.password.size() > kMaxPasswordLength) {
        return err(invalid_credentials());
    }
    auto credentials = credentials_.find_credentials_by_username(*username);
    if (!credentials) {
        if (credentials.error().status == domain::RepositoryStatus::NotFound) {
            // Keep the unknown-user path computationally close to a real
            // Argon2 verification so username existence is not exposed by a
            // cheap early return timing signal.
            auto dummy = password_hasher_.hash(command.password);
            if (!dummy) {
                return err(dummy.error());
            }
            return err(invalid_credentials());
        }
        return err(from_repository(credentials.error()));
    }
    auto verified = password_hasher_.verify(
        command.password, credentials->password_hash);
    if (!verified) {
        return err(verified.error());
    }
    if (!*verified) {
        return err(invalid_credentials());
    }
    if (!credentials->categories_initialized) {
        return err(Error::infrastructure_failure(
            "User registration is incomplete"));
    }

    auto pending = create_pending_tokens();
    if (!pending) {
        return err(pending.error());
    }
    const auto now = clock_.now();
    auto pair = build_token_pair(credentials->user.id(), *pending, now);
    if (!pair) {
        return pair;
    }
    auto uow = uow_factory_.create_for_user(credentials->user.id());
    if (!uow) {
        return err(Error::infrastructure_failure("Login transaction is unavailable"));
    }
    auto write = uow->execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto refresh = make_refresh_record(credentials->user.id(), *pending, now);
            if (auto saved = sessions_.save_refresh_token(tx, refresh); !saved) {
                return saved;
            }
            auto audit = auth_audit(
                credentials->user.id(),
                domain::AuditAction::Login,
                "AuthSession",
                pending->session_id,
                "{}",
                now);
            if (auto appended = audit_logs_.append(tx, audit); !appended) {
                return appended;
            }
            uow->register_event(std::make_shared<domain::UserLoggedInEvent>(
                credentials->user.id(), pending->session_id, now));
            return {};
        });
    if (!write) {
        return err(from_repository(write.error()));
    }
    return pair;
}

Result<TokenPairDto> AuthService::refresh(const RefreshCommand& command) {
    if (command.refresh_token.empty() ||
        command.refresh_token.size() > kMaxRefreshTokenLength) {
        return err(invalid_token());
    }
    auto old_hash = tokens_.hash_opaque_token(command.refresh_token);
    if (!old_hash) {
        return err(old_hash.error());
    }
    auto existing = sessions_.find_refresh_token(*old_hash);
    if (!existing) {
        if (existing.error().status == domain::RepositoryStatus::NotFound) {
            return err(invalid_token());
        }
        return err(from_repository(existing.error()));
    }

    const auto now = clock_.now();
    auto pending = create_pending_tokens();
    if (!pending) {
        return err(pending.error());
    }
    pending->session_id = existing->session_id;
    auto pair = build_token_pair(existing->user_id, *pending, now);
    if (!pair) {
        return pair;
    }
    auto uow = uow_factory_.create_for_user(existing->user_id);
    if (!uow) {
        return err(Error::infrastructure_failure("Refresh transaction is unavailable"));
    }

    std::optional<Error> response_error;
    auto write = uow->execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto locked = sessions_.find_refresh_token_for_update(tx, *old_hash);
            if (!locked) {
                return std::unexpected(locked.error());
            }

            auto revoked_session = sessions_.is_session_revoked(
                tx, locked->session_id, now);
            if (!revoked_session) {
                return std::unexpected(revoked_session.error());
            }
            if (*revoked_session) {
                response_error = invalid_token();
                return {};
            }

            if (locked->revoked_at.has_value()) {
                const auto session_expiry =
                    now + tokens_.refresh_token_lifetime();
                if (auto revoked = sessions_.revoke_session(
                        tx,
                        locked->user_id,
                        locked->session_id,
                        now,
                        session_expiry,
                        "refresh_token_reuse");
                    !revoked) {
                    return revoked;
                }
                auto audit = auth_audit(
                    locked->user_id,
                    domain::AuditAction::SecurityEvent,
                    "AuthSession",
                    locked->session_id,
                    "{\"reason\":\"refresh_token_reuse\"}",
                    now);
                if (auto appended = audit_logs_.append(tx, audit); !appended) {
                    return appended;
                }
                uow->register_event(
                    std::make_shared<domain::RefreshTokenReuseDetectedEvent>(
                        locked->user_id, locked->session_id, now));
                response_error = invalid_token();
                return {};
            }

            if (locked->expires_at <= now) {
                if (auto revoked = sessions_.revoke_refresh_token(
                        tx, locked->token_hash, now);
                    !revoked) {
                    return revoked;
                }
                response_error = Error(ErrorCode::ExpiredToken, "Refresh token expired");
                return {};
            }

            if (auto revoked = sessions_.revoke_refresh_token(
                    tx, locked->token_hash, now);
                !revoked) {
                return revoked;
            }
            auto replacement = make_refresh_record(locked->user_id, *pending, now);
            if (auto saved = sessions_.save_refresh_token(tx, replacement); !saved) {
                return saved;
            }
            auto audit = auth_audit(
                locked->user_id,
                domain::AuditAction::TokenRefresh,
                "AuthSession",
                locked->session_id,
                "{}",
                now);
            if (auto appended = audit_logs_.append(tx, audit); !appended) {
                return appended;
            }
            uow->register_event(std::make_shared<domain::TokenRefreshedEvent>(
                locked->user_id, locked->session_id, now));
            return {};
        });
    if (!write) {
        return err(from_repository(write.error()));
    }
    if (response_error.has_value()) {
        return err(*response_error);
    }
    return pair;
}

VoidResult AuthService::logout(const LogoutCommand& command) {
    if (!command.access_claims.user_id.is_valid() ||
        command.access_claims.session_id.empty() ||
        command.access_claims.token_id.empty() ||
        command.refresh_token.empty() ||
        command.refresh_token.size() > kMaxRefreshTokenLength) {
        return err(invalid_token());
    }
    auto refresh_hash = tokens_.hash_opaque_token(command.refresh_token);
    if (!refresh_hash) {
        return err(refresh_hash.error());
    }
    const auto now = clock_.now();
    auto uow = uow_factory_.create_for_user(command.access_claims.user_id);
    if (!uow) {
        return err(Error::infrastructure_failure("Logout transaction is unavailable"));
    }
    std::optional<Error> response_error;
    auto write = uow->execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto refresh = sessions_.find_refresh_token_for_update(tx, *refresh_hash);
            if (!refresh) {
                if (refresh.error().status == domain::RepositoryStatus::NotFound) {
                    response_error = invalid_token();
                    return std::unexpected(domain::RepositoryError::validation(
                        "Refresh token mismatch"));
                }
                return std::unexpected(refresh.error());
            }
            if (refresh->user_id != command.access_claims.user_id ||
                refresh->session_id != command.access_claims.session_id) {
                response_error = invalid_token();
                return std::unexpected(domain::RepositoryError::validation(
                    "Refresh token session mismatch"));
            }
            // Logout invalidates the whole login session, not only the refresh
            // token presented by this request. This closes the rotation race
            // where a concurrent refresh has already created a replacement
            // token in the same session.
            if (auto revoked = sessions_.revoke_session(
                    tx,
                    command.access_claims.user_id,
                    command.access_claims.session_id,
                    now,
                    now + tokens_.refresh_token_lifetime(),
                    "logout");
                !revoked) {
                return revoked;
            }
            if (auto revoked = sessions_.revoke_access_token(
                    tx, command.access_claims, now);
                !revoked) {
                return revoked;
            }
            auto audit = auth_audit(
                command.access_claims.user_id,
                domain::AuditAction::Logout,
                "AuthSession",
                command.access_claims.session_id,
                "{}",
                now);
            if (auto appended = audit_logs_.append(tx, audit); !appended) {
                return appended;
            }
            uow->register_event(std::make_shared<domain::UserLoggedOutEvent>(
                command.access_claims.user_id,
                command.access_claims.session_id,
                now));
            return {};
        });
    if (!write) {
        if (response_error.has_value()) {
            return err(*response_error);
        }
        return err(from_repository(write.error()));
    }
    return ok();
}

} // namespace pfh::application
