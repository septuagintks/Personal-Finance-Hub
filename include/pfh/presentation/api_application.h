// Personal Finance Hub - Framework-Neutral API Application

#pragma once

#include "pfh/presentation/controllers/auth_controller.h"
#include "pfh/presentation/controllers/resource_controllers.h"
#include "pfh/presentation/controllers/transaction_controller.h"
#include "pfh/presentation/controllers/transfer_controller.h"
#include "pfh/presentation/controllers/report_controller.h"
#include "pfh/presentation/http/http_types.h"
#include "pfh/presentation/security/jwt_filter.h"

#include <atomic>
#include <string>

namespace pfh::presentation {

class ApiApplication {
public:
    ApiApplication(AuthController& auth, JwtFilter& jwt_filter)
        : auth_(auth), jwt_filter_(jwt_filter) {}

    ApiApplication(
        AuthController& auth,
        JwtFilter& jwt_filter,
        AccountController& accounts,
        CategoryController& categories,
        TagController& tags,
        PreferenceController& preferences,
        CurrencyController& currencies,
        TransactionController& transactions,
        TransferController& transfers,
        ReportController& reports)
        : auth_(auth),
          jwt_filter_(jwt_filter),
          accounts_(&accounts),
          categories_(&categories),
          tags_(&tags),
          preferences_(&preferences),
          currencies_(&currencies),
          transactions_(&transactions),
          transfers_(&transfers),
          reports_(&reports) {}

    [[nodiscard]] HttpResponse handle(HttpRequest request) noexcept;
    [[nodiscard]] static std::string generate_trace_id();

private:
    AuthController& auth_;
    JwtFilter& jwt_filter_;
    AccountController* accounts_ = nullptr;
    CategoryController* categories_ = nullptr;
    TagController* tags_ = nullptr;
    PreferenceController* preferences_ = nullptr;
    CurrencyController* currencies_ = nullptr;
    TransactionController* transactions_ = nullptr;
    TransferController* transfers_ = nullptr;
    ReportController* reports_ = nullptr;
};

} // namespace pfh::presentation
