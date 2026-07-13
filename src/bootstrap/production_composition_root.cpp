// Personal Finance Hub - Production Composition Root

#include "pfh/bootstrap/production_composition_root.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/application/services/auth_service.h"
#include "pfh/application/services/finance_application_service.h"
#include "pfh/bootstrap/database_client_factory.h"
#include "pfh/infrastructure/persistence/audit_log_repository_impl.h"
#include "pfh/infrastructure/persistence/auth_session_repository_impl.h"
#include "pfh/infrastructure/persistence/drogon_unit_of_work_factory.h"
#include "pfh/infrastructure/persistence/postgres_active_currency_query.h"
#include "pfh/infrastructure/persistence/postgres_request_scope.h"
#include "pfh/infrastructure/persistence/registration_defaults_repository_impl.h"
#include "pfh/infrastructure/persistence/user_repository_impl.h"
#include "pfh/infrastructure/security/argon2_password_hasher.h"
#include "pfh/infrastructure/security/openssl_token_service.h"
#include "pfh/infrastructure/system_clock.h"
#include "pfh/presentation/api_application.h"
#include "pfh/presentation/controllers/auth_controller.h"
#include "pfh/presentation/controllers/resource_controllers.h"
#include "pfh/presentation/controllers/transaction_controller.h"
#include "pfh/presentation/controllers/transfer_controller.h"
#include "pfh/presentation/controllers/report_controller.h"
#include "pfh/presentation/drogon_http_adapter.h"
#include "pfh/presentation/security/jwt_filter.h"

#include <memory>
#include <openssl/crypto.h>
#include <string>
#include <utility>

namespace pfh::bootstrap {

namespace {

[[nodiscard]] bool missing_or_placeholder(std::string_view value) {
    return value.empty() || value.starts_with("REPLACE_WITH_");
}

} // namespace

ProductionCompositionRoot::ProductionCompositionRoot(
    infrastructure::AppConfig config)
    : config_(std::move(config)) {}

ProductionCompositionRoot::~ProductionCompositionRoot() {
    auto cleanse = [](std::string& value) {
        if (!value.empty()) {
            OPENSSL_cleanse(value.data(), value.size());
        }
    };
    cleanse(config_.jwt.secret);
    cleanse(config_.security.password_pepper);
    cleanse(config_.database.password);
    cleanse(config_.background_database.password);
}

application::VoidResult ProductionCompositionRoot::validate_config() const {
    if (missing_or_placeholder(config_.database.password) ||
        missing_or_placeholder(config_.background_database.password)) {
        return application::err(application::Error(
            application::ErrorCode::ConfigurationError,
            "Request and background database passwords are required"));
    }
    if (config_.database.user.empty() ||
        config_.background_database.user.empty() ||
        config_.database.user == config_.background_database.user) {
        return application::err(application::Error(
            application::ErrorCode::ConfigurationError,
            "Request and background database roles must be distinct"));
    }
    if (missing_or_placeholder(config_.jwt.secret) ||
        config_.jwt.secret.size() < 32) {
        return application::err(application::Error(
            application::ErrorCode::ConfigurationError,
            "JWT secret must contain at least 32 bytes"));
    }
    if (config_.security.password_pepper.starts_with("REPLACE_WITH_")) {
        return application::err(application::Error(
            application::ErrorCode::ConfigurationError,
            "Password pepper must be replaced or left empty"));
    }
    if (config_.security.password_pepper.size() > 1024) {
        return application::err(application::Error(
            application::ErrorCode::ConfigurationError,
            "Password pepper exceeds 1024 bytes"));
    }
    return application::ok();
}

application::VoidResult ProductionCompositionRoot::initialize() {
    if (auto valid = validate_config(); !valid) {
        return valid;
    }
    auto request = DatabaseClientFactory::create(config_.database);
    if (!request) {
        return application::err(request.error());
    }
    auto background = DatabaseClientFactory::create(config_.background_database);
    if (!background) {
        return application::err(background.error());
    }
    request_db_ = *request;
    background_db_ = *background;
    if (request_db_ == background_db_) {
        return application::err(application::Error(
            application::ErrorCode::ConfigurationError,
            "Request and background database clients must be distinct"));
    }
    if (auto verified = DatabaseClientFactory::verify_request_role(
            request_db_, config_.database.user);
        !verified) {
        return verified;
    }
    if (auto verified = DatabaseClientFactory::verify_background_role(
            background_db_, config_.background_database.user);
        !verified) {
        return verified;
    }

    clock_ = std::make_unique<infrastructure::SystemClock>();
    password_hasher_ = std::make_unique<infrastructure::Argon2PasswordHasher>(
        config_.security.password_pepper);
    token_service_ = std::make_unique<infrastructure::OpenSslTokenService>(
        config_.jwt.secret,
        config_.jwt.issuer,
        config_.jwt.audience,
        config_.jwt.access_token_expiry,
        config_.jwt.refresh_token_expiry,
        config_.jwt.clock_skew);
    users_ = std::make_unique<infrastructure::UserRepositoryImpl>(request_db_);
    registration_defaults_ =
        std::make_unique<infrastructure::RegistrationDefaultsRepositoryImpl>();
    sessions_ = std::make_unique<infrastructure::AuthSessionRepositoryImpl>(
        request_db_);
    audit_logs_ = std::make_unique<infrastructure::AuditLogRepositoryImpl>();
    uow_factory_ = std::make_unique<infrastructure::DrogonUnitOfWorkFactory>(
        request_db_);

    // The privileged client is captured only by this background adapter. It is
    // intentionally never passed to AuthService, JwtFilter, controllers, or
    // any request-scoped repository factory.
    background_currency_query_ =
        std::make_unique<infrastructure::PostgresActiveCurrencyQuery>(
            background_db_);

    auth_service_ = std::make_unique<application::AuthService>(
        *users_,
        *users_,
        *registration_defaults_,
        *sessions_,
        *audit_logs_,
        *uow_factory_,
        *password_hasher_,
        *token_service_,
        *clock_);
    request_scope_factory_ =
        std::make_unique<infrastructure::PostgresRequestScopeFactory>(request_db_);
    finance_service_ = std::make_unique<application::FinanceApplicationService>(
        *request_scope_factory_, *clock_);
    auth_controller_ = std::make_unique<presentation::AuthController>(
        *auth_service_);
    account_controller_ = std::make_unique<presentation::AccountController>(
        *finance_service_);
    category_controller_ = std::make_unique<presentation::CategoryController>(
        *finance_service_);
    tag_controller_ = std::make_unique<presentation::TagController>(
        *finance_service_);
    preference_controller_ = std::make_unique<presentation::PreferenceController>(
        *finance_service_);
    currency_controller_ = std::make_unique<presentation::CurrencyController>(
        *finance_service_);
    transaction_controller_ =
        std::make_unique<presentation::TransactionController>(*finance_service_);
    transfer_controller_ =
        std::make_unique<presentation::TransferController>(*finance_service_);
    report_controller_ =
        std::make_unique<presentation::ReportController>(*finance_service_);
    jwt_filter_ = std::make_unique<presentation::JwtFilter>(
        *token_service_, *sessions_, *clock_);
    api_application_ = std::make_unique<presentation::ApiApplication>(
        *auth_controller_,
        *jwt_filter_,
        *account_controller_,
        *category_controller_,
        *tag_controller_,
        *preference_controller_,
        *currency_controller_,
        *transaction_controller_,
        *transfer_controller_,
        *report_controller_);
    http_adapter_ = std::make_unique<presentation::DrogonHttpAdapter>(
        *api_application_,
        presentation::HttpServerConfig{
            config_.server.host,
            config_.server.port,
            config_.server.threads});
    http_adapter_->configure();
    return application::ok();
}

void ProductionCompositionRoot::run() {
    http_adapter_->run();
}

} // namespace pfh::bootstrap

#endif // PFH_HAS_POSTGRESQL
