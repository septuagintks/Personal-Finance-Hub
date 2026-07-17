// Personal Finance Hub - Report Controller

#pragma once

#include "pfh/application/services/finance_application_service.h"
#include "pfh/presentation/http/http_types.h"

namespace pfh::presentation {

class ReportController {
public:
    explicit ReportController(application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse net_worth(const HttpRequest& request);
    [[nodiscard]] HttpResponse cash_flow(const HttpRequest& request);
    [[nodiscard]] HttpResponse dashboard_summary(const HttpRequest& request);
    [[nodiscard]] HttpResponse analysis(const HttpRequest& request);
    [[nodiscard]] HttpResponse export_transactions(const HttpRequest& request);

private:
    application::FinanceApplicationService& service_;
};

} // namespace pfh::presentation
