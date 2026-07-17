// Personal Finance Hub - Authenticated User Maintenance Controller

#pragma once

#include "pfh/application/services/finance_application_service.h"
#include "pfh/presentation/http/http_types.h"

#include <string_view>

namespace pfh::presentation {

class MaintenanceController {
public:
    explicit MaintenanceController(
        application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse list_audit_logs(const HttpRequest& request);
    [[nodiscard]] HttpResponse rebuild_all_balance_caches(
        const HttpRequest& request);
    [[nodiscard]] HttpResponse rebuild_account_balance_cache(
        const HttpRequest& request,
        std::string_view account_id);

private:
    application::FinanceApplicationService& service_;
};

} // namespace pfh::presentation
