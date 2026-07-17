// Personal Finance Hub - Transfer Controller

#pragma once

#include "pfh/application/services/finance_application_service.h"
#include "pfh/presentation/http/http_types.h"

#include <string_view>

namespace pfh::presentation {

class TransferController {
public:
    explicit TransferController(application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse create(const HttpRequest& request);
    [[nodiscard]] HttpResponse list(const HttpRequest& request);
    [[nodiscard]] HttpResponse get(
        const HttpRequest& request,
        std::string_view transfer_group_id);
    [[nodiscard]] HttpResponse correct(
        const HttpRequest& request,
        std::string_view transfer_group_id);
    [[nodiscard]] HttpResponse remove(
        const HttpRequest& request,
        std::string_view transfer_group_id);

private:
    application::FinanceApplicationService& service_;
};

} // namespace pfh::presentation
