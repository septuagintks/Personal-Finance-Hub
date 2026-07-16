// Personal Finance Hub - Production Composition Root

#include "pfh/bootstrap/production_composition_root.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/application/services/auth_service.h"
#include "pfh/application/services/finance_application_service.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/events/local_event_bus.h"
#include "pfh/application/events/outbox_publisher.h"
#include "pfh/application/events/supplemental_audit_handler.h"
#include "pfh/application/maintenance/cleanup_expired_sessions_use_case.h"
#include "pfh/application/use_cases/refresh_exchange_rates_use_case.h"
#include "pfh/bootstrap/database_client_factory.h"
#include "pfh/infrastructure/external/curl_http_transport.h"
#include "pfh/infrastructure/external/exchange_rate_providers.h"
#include "pfh/infrastructure/persistence/audit_log_repository_impl.h"
#include "pfh/infrastructure/persistence/auth_session_repository_impl.h"
#include "pfh/infrastructure/persistence/drogon_unit_of_work.h"
#include "pfh/infrastructure/persistence/drogon_unit_of_work_factory.h"
#include "pfh/infrastructure/persistence/exchange_rate_repository_impl.h"
#include "pfh/infrastructure/persistence/postgres_active_currency_query.h"
#include "pfh/infrastructure/persistence/postgres_job_lease_repository.h"
#include "pfh/infrastructure/persistence/postgres_outbox_repository.h"
#include "pfh/infrastructure/persistence/postgres_request_scope.h"
#include "pfh/infrastructure/persistence/postgres_session_cleanup_repository.h"
#include "pfh/infrastructure/persistence/postgres_supplemental_audit_store.h"
#include "pfh/infrastructure/persistence/registration_defaults_repository_impl.h"
#include "pfh/infrastructure/scheduler/bounded_thread_pool.h"
#include "pfh/infrastructure/scheduler/drogon_timer_scheduler.h"
#include "pfh/infrastructure/scheduler/job_manager.h"
#include "pfh/infrastructure/scheduler/scheduled_jobs.h"
#include "pfh/infrastructure/persistence/user_repository_impl.h"
#include "pfh/infrastructure/security/argon2_password_hasher.h"
#include "pfh/infrastructure/security/openssl_token_service.h"
#include "pfh/infrastructure/security/openssl_request_hasher.h"
#include "pfh/infrastructure/system_clock.h"
#include "pfh/presentation/api_application.h"
#include "pfh/presentation/controllers/auth_controller.h"
#include "pfh/presentation/controllers/resource_controllers.h"
#include "pfh/presentation/controllers/transaction_controller.h"
#include "pfh/presentation/controllers/transfer_controller.h"
#include "pfh/presentation/controllers/report_controller.h"
#include "pfh/presentation/drogon_http_adapter.h"
#include "pfh/presentation/security/jwt_filter.h"

#include <chrono>
#include <cstdint>
#include <drogon/drogon.h>
#include <limits>
#include <memory>
#include <openssl/crypto.h>
#include <spdlog/spdlog.h>
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
    if (request_executor_) {
        request_executor_->shutdown();
    }
    if (job_manager_) {
        job_manager_->stop_all();
    }
    if (background_executor_) {
        background_executor_->shutdown();
    }
    auto cleanse = [](std::string& value) {
        if (!value.empty()) {
            OPENSSL_cleanse(value.data(), value.size());
        }
    };
    cleanse(config_.jwt.secret);
    cleanse(config_.security.password_pepper);
    cleanse(config_.database.password);
    cleanse(config_.background_database.password);
    cleanse(config_.exchange_rate.api_key);
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
    if (config_.server.request_worker_threads == 0 ||
        config_.server.request_worker_threads > 64 ||
        config_.server.request_queue_capacity == 0 ||
        config_.server.request_queue_capacity > 10000) {
        return application::err(application::Error(
            application::ErrorCode::ConfigurationError,
            "HTTP request worker configuration is invalid"));
    }
    if (config_.scheduler.enabled) {
        if (config_.scheduler.worker_threads == 0 ||
            config_.scheduler.worker_threads > 64 ||
            config_.scheduler.queue_capacity == 0 ||
            config_.scheduler.queue_capacity > 10000 ||
            config_.scheduler.outbox_batch_size == 0 ||
            config_.scheduler.outbox_batch_size >
                static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()) ||
            config_.scheduler.session_cleanup_batch_size == 0 ||
            config_.scheduler.session_cleanup_batch_size >
                static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()) ||
            config_.scheduler.outbox_publish_interval <=
                std::chrono::seconds::zero() ||
            config_.scheduler.outbox_processing_timeout <=
                config_.scheduler.job_execution_timeout ||
            config_.scheduler.exchange_rate_refresh_interval <=
                std::chrono::minutes::zero() ||
            config_.scheduler.session_cleanup_interval <=
                std::chrono::minutes::zero() ||
            config_.scheduler.job_execution_timeout <=
                std::chrono::seconds::zero() ||
            config_.scheduler.job_lease_duration <=
                config_.scheduler.job_execution_timeout) {
            return application::err(application::Error(
                application::ErrorCode::ConfigurationError,
                "Scheduler configuration is invalid"));
        }
        if (config_.exchange_rate.provider != "freecurrencyapi" ||
            missing_or_placeholder(config_.exchange_rate.api_key) ||
            config_.exchange_rate.api_key.size() > 512 ||
            config_.exchange_rate.request_timeout <=
                std::chrono::seconds::zero() ||
            config_.exchange_rate.request_timeout >
                config_.scheduler.job_execution_timeout / 2) {
            return application::err(application::Error(
                application::ErrorCode::ConfigurationError,
                "Production exchange-rate provider configuration is invalid"));
        }
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
    request_hasher_ =
        std::make_unique<infrastructure::OpenSslRequestHasher>();
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

    if (config_.scheduler.enabled) {
        auto instance_token = token_service_->generate_opaque_token(18);
        if (!instance_token) {
            return application::err(instance_token.error());
        }
        scheduler_instance_id_ = "scheduler-" + *instance_token;

        // All state-changing background adapters use the ordinary application
        // role against non-RLS tables. The BYPASSRLS client remains confined
        // to PostgresActiveCurrencyQuery above.
        primary_rate_http_transport_ =
            std::make_unique<infrastructure::CurlHttpTransport>(
                "https://api.freecurrencyapi.com");
        fallback_rate_http_transport_ =
            std::make_unique<infrastructure::CurlHttpTransport>(
                "https://api.exchangerate.fun");
        primary_rate_provider_ =
            std::make_unique<infrastructure::FreeCurrencyApiProvider>(
                *primary_rate_http_transport_,
                *clock_,
                std::move(config_.exchange_rate.api_key),
                config_.exchange_rate.request_timeout);
        fallback_rate_provider_ =
            std::make_unique<infrastructure::ExchangeRateFunProvider>(
                *fallback_rate_http_transport_,
                *clock_,
                config_.exchange_rate.request_timeout);
        rate_provider_ =
            std::make_unique<infrastructure::FailoverExchangeRateProvider>(
                *primary_rate_provider_,
                *fallback_rate_provider_);
        rate_repository_ =
            std::make_unique<infrastructure::ExchangeRateRepositoryImpl>(
                request_db_);
        rate_uow_ = std::make_unique<infrastructure::DrogonUnitOfWork>(
            request_db_);
        refresh_rates_use_case_ =
            std::make_unique<application::RefreshExchangeRatesUseCase>(
                *background_currency_query_,
                *rate_repository_,
                *rate_provider_,
                *rate_uow_,
                *clock_);

        outbox_repository_ =
            std::make_unique<infrastructure::PostgresOutboxRepository>(
                request_db_);
        supplemental_audit_store_ =
            std::make_unique<infrastructure::PostgresSupplementalAuditStore>(
                request_db_);
        supplemental_audit_handler_ =
            std::make_shared<application::SupplementalAuditHandler>(
                *supplemental_audit_store_);
        event_bus_ = std::make_unique<application::LocalEventBus>();
        event_bus_->subscribe(supplemental_audit_handler_);
        outbox_publisher_ = std::make_unique<application::OutboxPublisher>(
            *outbox_repository_,
            *event_bus_,
            *clock_,
            application::OutboxPublisherConfig{
                config_.scheduler.outbox_batch_size,
                config_.scheduler.outbox_processing_timeout,
                config_.scheduler.outbox_batch_size},
            supplemental_audit_handler_.get());

        session_cleanup_repository_ =
            std::make_unique<
                infrastructure::PostgresSessionCleanupRepository>(request_db_);
        cleanup_sessions_use_case_ = std::make_unique<
            application::CleanupExpiredSessionsUseCase>(
                *session_cleanup_repository_,
                *clock_,
                config_.scheduler.session_cleanup_batch_size);
        job_lease_repository_ =
            std::make_unique<infrastructure::PostgresJobLeaseRepository>(
                request_db_);
        background_executor_ =
            std::make_unique<infrastructure::BoundedThreadPool>(
                config_.scheduler.worker_threads,
                config_.scheduler.queue_capacity);
        timer_scheduler_ =
            std::make_unique<infrastructure::DrogonTimerScheduler>();

        const infrastructure::RecurringJobConfig outbox_config{
            {},
            config_.scheduler.outbox_publish_interval,
            config_.scheduler.job_execution_timeout,
            config_.scheduler.job_lease_duration,
            true};
        const infrastructure::RecurringJobConfig rate_config{
            {},
            config_.scheduler.exchange_rate_refresh_interval,
            config_.scheduler.job_execution_timeout,
            config_.scheduler.job_lease_duration,
            true};
        const infrastructure::RecurringJobConfig cleanup_config{
            {},
            config_.scheduler.session_cleanup_interval,
            config_.scheduler.job_execution_timeout,
            config_.scheduler.job_lease_duration,
            true};
        outbox_job_ =
            std::make_shared<infrastructure::OutboxPublisherJob>(
                *timer_scheduler_,
                *background_executor_,
                *clock_,
                *outbox_publisher_,
                scheduler_instance_id_ + "-outbox",
                outbox_config);
        rate_refresh_job_ =
            std::make_shared<infrastructure::ExchangeRateRefreshJob>(
                *timer_scheduler_,
                *background_executor_,
                *clock_,
                *refresh_rates_use_case_,
                *job_lease_repository_,
                scheduler_instance_id_,
                rate_config);
        session_cleanup_job_ =
            std::make_shared<infrastructure::SessionCleanupJob>(
                *timer_scheduler_,
                *background_executor_,
                *clock_,
                *cleanup_sessions_use_case_,
                *job_lease_repository_,
                scheduler_instance_id_,
                cleanup_config);
        job_manager_ = std::make_unique<infrastructure::JobManager>();
        for (const auto& job : {
                 std::static_pointer_cast<application::IJob>(outbox_job_),
                 std::static_pointer_cast<application::IJob>(rate_refresh_job_),
                 std::static_pointer_cast<application::IJob>(
                     session_cleanup_job_)}) {
            auto registered = job_manager_->register_job(job);
            if (!registered) {
                return application::err(
                    application::from_repository(registered.error()));
            }
        }

        drogon::app().registerBeginningAdvice([this] {
            auto started = job_manager_->start_all();
            if (!started) {
                spdlog::critical(
                    "Failed to start background jobs error={}",
                    started.error().message);
                drogon::app().quit();
            }
        });
    }

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
        *request_scope_factory_, *clock_, *request_hasher_);
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
    request_executor_ =
        std::make_unique<infrastructure::BoundedThreadPool>(
            config_.server.request_worker_threads,
            config_.server.request_queue_capacity);
    http_adapter_ = std::make_unique<presentation::DrogonHttpAdapter>(
        *api_application_,
        *request_executor_,
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
