// Personal Finance Hub - Transaction Controller

#pragma once

#include "pfh/application/services/finance_application_service.h"
#include "pfh/presentation/http/http_types.h"

#include <string_view>

namespace pfh::presentation {

class TransactionController {
public:
    explicit TransactionController(application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse create(const HttpRequest& request);
    [[nodiscard]] HttpResponse list(const HttpRequest& request);
    [[nodiscard]] HttpResponse get(
        const HttpRequest& request,
        std::string_view transaction_id);
    [[nodiscard]] HttpResponse correct(
        const HttpRequest& request,
        std::string_view transaction_id);
    [[nodiscard]] HttpResponse remove(
        const HttpRequest& request,
        std::string_view transaction_id);

private:
    application::FinanceApplicationService& service_;
};

} // namespace pfh::presentation
