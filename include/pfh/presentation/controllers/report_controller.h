// Personal Finance Hub - Report Controller

#pragma once

#include "pfh/application/services/finance_application_service.h"
#include "pfh/presentation/http/http_types.h"
#include "pfh/presentation/http/report_resource_metrics.h"

namespace pfh::presentation {

class ReportController {
public:
    explicit ReportController(
        application::FinanceApplicationService& service,
        ReportResourceMetrics& resource_metrics)
        : service_(service), resource_metrics_(resource_metrics) {}

    [[nodiscard]] HttpResponse net_worth(const HttpRequest& request);
    [[nodiscard]] HttpResponse cash_flow(const HttpRequest& request);
    [[nodiscard]] HttpResponse dashboard_summary(const HttpRequest& request);
    [[nodiscard]] HttpResponse analysis(const HttpRequest& request);
    [[nodiscard]] HttpResponse export_transactions(const HttpRequest& request);

private:
    application::FinanceApplicationService& service_;
    ReportResourceMetrics& resource_metrics_;
};

} // namespace pfh::presentation
