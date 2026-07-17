// Personal Finance Hub - Framework-Neutral API Application

#include "pfh/presentation/api_application.h"

#include "pfh/presentation/http/http_response_mapper.h"

#include <chrono>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

namespace pfh::presentation {

namespace {

[[nodiscard]] std::optional<std::string_view> route_id(
    std::string_view path,
    std::string_view prefix,
    std::string_view suffix = {}) {
    if (!path.starts_with(prefix) || !path.ends_with(suffix) ||
        path.size() <= prefix.size() + suffix.size()) {
        return std::nullopt;
    }
    const auto id = path.substr(
        prefix.size(), path.size() - prefix.size() - suffix.size());
    if (id.empty() || id.find('/') != std::string_view::npos) {
        return std::nullopt;
    }
    return id;
}

} // namespace

std::string ApiApplication::generate_trace_id() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "trace-" + std::to_string(micros) + "-" +
           std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

HttpResponse ApiApplication::handle(HttpRequest request) noexcept {
    if (request.trace_id.empty()) {
        request.trace_id = generate_trace_id();
    }
    const bool prevent_storage =
        !JwtFilter::is_public_route(request.method, request.path) ||
        request.path.starts_with("/api/v1/auth/") ||
        request.path.starts_with("/api/v1/web/auth/");
    auto finalize = [&](HttpResponse response) {
        response.headers.insert_or_assign("X-Trace-Id", request.trace_id);
        if (prevent_storage) {
            response.headers.insert_or_assign("Cache-Control", "no-store");
        }
        return response;
    };
    try {
        if (!JwtFilter::is_public_route(request.method, request.path)) {
            auto identity = jwt_filter_.authenticate(request);
            if (!identity) {
                return finalize(HttpResponseMapper::error(
                    identity.error(), request.trace_id));
            }
            request.identity = *identity;
        }

        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/auth/register") {
            return finalize(auth_.register_user(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/auth/login") {
            return finalize(auth_.login(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/auth/refresh") {
            return finalize(auth_.refresh(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/auth/logout") {
            return finalize(auth_.logout(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/web/auth/register") {
            return finalize(auth_.web_register_user(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/web/auth/login") {
            return finalize(auth_.web_login(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/web/auth/refresh") {
            return finalize(auth_.web_refresh(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/web/auth/logout") {
            return finalize(auth_.web_logout(request));
        }
        if (request.method == HttpMethod::Get &&
            request.path == "/api/v1/currencies" && currencies_ != nullptr) {
            return finalize(currencies_->list(request));
        }
        if (request.method == HttpMethod::Get &&
            request.path == "/api/v1/accounts" && accounts_ != nullptr) {
            return finalize(accounts_->list(request));
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/accounts" && accounts_ != nullptr) {
            return finalize(accounts_->create(request));
        }
        if (accounts_ != nullptr) {
            if (const auto id = route_id(
                    request.path, "/api/v1/accounts/", "/balance");
                id.has_value() && request.method == HttpMethod::Get) {
                return finalize(accounts_->balance(request, *id));
            }
            if (const auto id = route_id(
                    request.path, "/api/v1/accounts/", "/archive");
                id.has_value() && request.method == HttpMethod::Post) {
                return finalize(accounts_->archive(request, *id));
            }
            if (const auto id = route_id(
                    request.path, "/api/v1/accounts/", "/restore");
                id.has_value() && request.method == HttpMethod::Post) {
                return finalize(accounts_->restore(request, *id));
            }
            if (const auto id = route_id(request.path, "/api/v1/accounts/");
                id.has_value()) {
                if (request.method == HttpMethod::Get) {
                    return finalize(accounts_->get(request, *id));
                }
                if (request.method == HttpMethod::Put) {
                    return finalize(accounts_->update(request, *id));
                }
                if (request.method == HttpMethod::Delete) {
                    return finalize(accounts_->dangerous_delete(request, *id));
                }
            }
        }
        if (request.path == "/api/v1/categories" && categories_ != nullptr) {
            if (request.method == HttpMethod::Get) {
                return finalize(categories_->list(request));
            }
            if (request.method == HttpMethod::Post) {
                return finalize(categories_->create(request));
            }
        }
        if (categories_ != nullptr) {
            if (const auto id = route_id(request.path, "/api/v1/categories/");
                id.has_value() && request.method == HttpMethod::Delete) {
                return finalize(categories_->remove(request, *id));
            }
        }
        if (request.path == "/api/v1/tags" && tags_ != nullptr) {
            if (request.method == HttpMethod::Get) {
                return finalize(tags_->list(request));
            }
            if (request.method == HttpMethod::Post) {
                return finalize(tags_->create(request));
            }
        }
        if (tags_ != nullptr) {
            if (const auto id = route_id(request.path, "/api/v1/tags/");
                id.has_value() && request.method == HttpMethod::Delete) {
                return finalize(tags_->remove(request, *id));
            }
            if (const auto id = route_id(
                    request.path, "/api/v1/transactions/", "/tags");
                id.has_value() && request.method == HttpMethod::Put) {
                return finalize(tags_->replace_transaction_tags(request, *id));
            }
        }
        if (request.path == "/api/v1/users/me/preferences" &&
            preferences_ != nullptr) {
            if (request.method == HttpMethod::Get) {
                return finalize(preferences_->get(request));
            }
            if (request.method == HttpMethod::Put) {
                return finalize(preferences_->update(request));
            }
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/transactions" &&
            transactions_ != nullptr) {
            return finalize(transactions_->create(request));
        }
        if (transactions_ != nullptr) {
            if (const auto id = route_id(request.path, "/api/v1/transactions/");
                id.has_value() && request.method == HttpMethod::Delete) {
                return finalize(transactions_->remove(request, *id));
            }
        }
        if (request.method == HttpMethod::Post &&
            request.path == "/api/v1/transfers" && transfers_ != nullptr) {
            return finalize(transfers_->create(request));
        }
        if (transfers_ != nullptr) {
            if (const auto id = route_id(request.path, "/api/v1/transfers/");
                id.has_value() && request.method == HttpMethod::Get) {
                return finalize(transfers_->get(request, *id));
            }
        }
        if (reports_ != nullptr && request.method == HttpMethod::Get) {
            if (request.path == "/api/v1/reports/net-worth") {
                return finalize(reports_->net_worth(request));
            }
            if (request.path == "/api/v1/reports/cash-flow") {
                return finalize(reports_->cash_flow(request));
            }
            if (request.path == "/api/v1/reports/dashboard-summary") {
                return finalize(reports_->dashboard_summary(request));
            }
        }
        return finalize(HttpResponseMapper::not_found(request.trace_id));
    } catch (const std::exception&) {
        return finalize(HttpResponseMapper::unexpected(request.trace_id));
    } catch (...) {
        return finalize(HttpResponseMapper::unexpected(request.trace_id));
    }
}

} // namespace pfh::presentation
