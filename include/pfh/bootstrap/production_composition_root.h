// Personal Finance Hub - Production Composition Root

#pragma once

#include "pfh/application/error.h"
#include "pfh/infrastructure/config.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <memory>

namespace pfh::application {
class AuthService;
}

namespace pfh::infrastructure {
class Argon2PasswordHasher;
class AuditLogRepositoryImpl;
class AuthSessionRepositoryImpl;
class DrogonUnitOfWorkFactory;
class OpenSslTokenService;
class PostgresActiveCurrencyQuery;
class PostgresRequestScopeFactory;
class RegistrationDefaultsRepositoryImpl;
class SystemClock;
class UserRepositoryImpl;
}

namespace pfh::presentation {
class AccountController;
class ApiApplication;
class AuthController;
class CategoryController;
class CurrencyController;
class DrogonHttpAdapter;
class JwtFilter;
class PreferenceController;
class TagController;
class TransactionController;
class TransferController;
class ReportController;
}

namespace pfh::application {
class FinanceApplicationService;
}

namespace pfh::bootstrap {

class ProductionCompositionRoot {
public:
    explicit ProductionCompositionRoot(infrastructure::AppConfig config);
    ~ProductionCompositionRoot();

    [[nodiscard]] application::VoidResult initialize();
    void run();

private:
    [[nodiscard]] application::VoidResult validate_config() const;

    infrastructure::AppConfig config_;
    drogon::orm::DbClientPtr request_db_;
    drogon::orm::DbClientPtr background_db_;

    std::unique_ptr<infrastructure::SystemClock> clock_;
    std::unique_ptr<infrastructure::Argon2PasswordHasher> password_hasher_;
    std::unique_ptr<infrastructure::OpenSslTokenService> token_service_;
    std::unique_ptr<infrastructure::UserRepositoryImpl> users_;
    std::unique_ptr<infrastructure::RegistrationDefaultsRepositoryImpl>
        registration_defaults_;
    std::unique_ptr<infrastructure::AuthSessionRepositoryImpl> sessions_;
    std::unique_ptr<infrastructure::AuditLogRepositoryImpl> audit_logs_;
    std::unique_ptr<infrastructure::DrogonUnitOfWorkFactory> uow_factory_;
    std::unique_ptr<infrastructure::PostgresActiveCurrencyQuery>
        background_currency_query_;
    std::unique_ptr<application::AuthService> auth_service_;
    std::unique_ptr<infrastructure::PostgresRequestScopeFactory>
        request_scope_factory_;
    std::unique_ptr<application::FinanceApplicationService> finance_service_;
    std::unique_ptr<presentation::AuthController> auth_controller_;
    std::unique_ptr<presentation::AccountController> account_controller_;
    std::unique_ptr<presentation::CategoryController> category_controller_;
    std::unique_ptr<presentation::TagController> tag_controller_;
    std::unique_ptr<presentation::PreferenceController> preference_controller_;
    std::unique_ptr<presentation::CurrencyController> currency_controller_;
    std::unique_ptr<presentation::TransactionController> transaction_controller_;
    std::unique_ptr<presentation::TransferController> transfer_controller_;
    std::unique_ptr<presentation::ReportController> report_controller_;
    std::unique_ptr<presentation::JwtFilter> jwt_filter_;
    std::unique_ptr<presentation::ApiApplication> api_application_;
    std::unique_ptr<presentation::DrogonHttpAdapter> http_adapter_;
};

} // namespace pfh::bootstrap

#endif // PFH_HAS_POSTGRESQL
