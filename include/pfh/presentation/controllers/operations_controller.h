// Personal Finance Hub - Health and Operator Controller

#pragma once

#include "pfh/application/services/operations_application_service.h"
#include "pfh/presentation/http/http_types.h"

#include <string_view>

namespace pfh::presentation {

class OperationsController {
public:
    explicit OperationsController(
        application::OperationsApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse liveness(const HttpRequest& request) const;
    [[nodiscard]] HttpResponse readiness(const HttpRequest& request);
    [[nodiscard]] HttpResponse summary(const HttpRequest& request);
    [[nodiscard]] HttpResponse metrics(const HttpRequest& request);
    [[nodiscard]] HttpResponse list_dead_letters(const HttpRequest& request);
    [[nodiscard]] HttpResponse retry_dead_letter(
        const HttpRequest& request,
        std::string_view outbox_id);

private:
    application::OperationsApplicationService& service_;
};

} // namespace pfh::presentation
