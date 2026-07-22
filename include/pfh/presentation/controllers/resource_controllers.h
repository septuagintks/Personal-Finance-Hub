// Personal Finance Hub - Foundational Resource Controllers

#pragma once

#include "pfh/application/services/finance_application_service.h"
#include "pfh/presentation/http/http_types.h"

#include <string_view>

namespace pfh::presentation {

class AccountController {
public:
    explicit AccountController(application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse list(const HttpRequest& request);
    [[nodiscard]] HttpResponse create(const HttpRequest& request);
    [[nodiscard]] HttpResponse get(
        const HttpRequest& request, std::string_view account_id);
    [[nodiscard]] HttpResponse update(
        const HttpRequest& request, std::string_view account_id);
    [[nodiscard]] HttpResponse balance(
        const HttpRequest& request, std::string_view account_id);
    [[nodiscard]] HttpResponse archive(
        const HttpRequest& request, std::string_view account_id);
    [[nodiscard]] HttpResponse restore(
        const HttpRequest& request, std::string_view account_id);
    [[nodiscard]] HttpResponse dangerous_delete(
        const HttpRequest& request, std::string_view account_id);

private:
    application::FinanceApplicationService& service_;
};

class CategoryController {
public:
    explicit CategoryController(application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse list(const HttpRequest& request);
    [[nodiscard]] HttpResponse create(const HttpRequest& request);
    [[nodiscard]] HttpResponse update(
        const HttpRequest& request, std::string_view category_id);
    [[nodiscard]] HttpResponse restore(
        const HttpRequest& request, std::string_view category_id);
    [[nodiscard]] HttpResponse remove(
        const HttpRequest& request, std::string_view category_id);

private:
    application::FinanceApplicationService& service_;
};

class TagController {
public:
    explicit TagController(application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse list(const HttpRequest& request);
    [[nodiscard]] HttpResponse create(const HttpRequest& request);
    [[nodiscard]] HttpResponse update(
        const HttpRequest& request, std::string_view tag_id);
    [[nodiscard]] HttpResponse restore(
        const HttpRequest& request, std::string_view tag_id);
    [[nodiscard]] HttpResponse remove(
        const HttpRequest& request, std::string_view tag_id);
    [[nodiscard]] HttpResponse replace_transaction_tags(
        const HttpRequest& request, std::string_view transaction_id);

private:
    application::FinanceApplicationService& service_;
};

class PreferenceController {
public:
    explicit PreferenceController(application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse get(const HttpRequest& request);
    [[nodiscard]] HttpResponse update(const HttpRequest& request);

private:
    application::FinanceApplicationService& service_;
};

class CurrencyController {
public:
    explicit CurrencyController(application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse list(const HttpRequest& request);

private:
    application::FinanceApplicationService& service_;
};

class TimeZoneController {
public:
    explicit TimeZoneController(application::FinanceApplicationService& service)
        : service_(service) {}

    [[nodiscard]] HttpResponse list(const HttpRequest& request);

private:
    application::FinanceApplicationService& service_;
};

} // namespace pfh::presentation
