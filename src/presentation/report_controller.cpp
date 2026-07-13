// Personal Finance Hub - Report Controller

#include "pfh/presentation/controllers/report_controller.h"

#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/time_codec.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace pfh::presentation {

namespace {

struct ParsedMonth {
    int year = 0;
    unsigned month = 0;
};

[[nodiscard]] application::Result<domain::UserId> require_user(
    const HttpRequest& request) {
    if (!request.identity.has_value() ||
        !request.identity->access_claims.user_id.is_valid()) {
        return application::err(application::Error::unauthorized());
    }
    return request.identity->access_claims.user_id;
}

[[nodiscard]] application::Result<ParsedMonth> parse_month(
    std::string_view value,
    std::string_view field) {
    if (value.size() != 7 || value[4] != '-') {
        return application::err(application::Error::validation(
            std::string(field) + " must use YYYY-MM"));
    }
    int year = 0;
    unsigned month = 0;
    const auto year_result = std::from_chars(
        value.data(), value.data() + 4, year);
    const auto month_result = std::from_chars(
        value.data() + 5, value.data() + 7, month);
    if (year_result.ec != std::errc{} ||
        year_result.ptr != value.data() + 4 ||
        month_result.ec != std::errc{} ||
        month_result.ptr != value.data() + 7 ||
        year < 1 || month < 1 || month > 12) {
        return application::err(application::Error::validation(
            std::string(field) + " must use a valid YYYY-MM"));
    }
    return ParsedMonth{year, month};
}

[[nodiscard]] nlohmann::json net_worth_json(
    const application::NetWorthDto& value) {
    return nlohmann::json{
        {"baseCurrency", value.currency_code},
        {"totalAssets", value.total_assets},
        {"totalLiabilities", value.total_liabilities},
        {"netWorth", value.total},
        {"generatedAt", TimeCodec::format_rfc3339(value.generated_at)}};
}

} // namespace

HttpResponse ReportController::net_worth(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    auto result = service_.net_worth(*user);
    return result ? HttpResponseMapper::json(200, net_worth_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse ReportController::cash_flow(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    const auto start_value = request.query.find("startDate");
    const auto end_value = request.query.find("endDate");
    const auto period_type = request.query.find("periodType");
    if (start_value == request.query.end() || end_value == request.query.end() ||
        period_type == request.query.end()) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "startDate, endDate, and periodType are required"),
            request.trace_id);
    }
    if (period_type->second != "MONTH") {
        return HttpResponseMapper::error(
            application::Error::validation(
                "Phase 1 cash flow supports periodType=MONTH"),
            request.trace_id);
    }
    auto start = parse_month(start_value->second, "startDate");
    auto end = parse_month(end_value->second, "endDate");
    if (!start) return HttpResponseMapper::error(start.error(), request.trace_id);
    if (!end) return HttpResponseMapper::error(end.error(), request.trace_id);
    auto result = service_.cash_flow_trend(application::CashFlowTrendQuery{
        *user, start->year, start->month, end->year, end->month});
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    nlohmann::json trends = nlohmann::json::array();
    for (const auto& period : result->trends) {
        trends.push_back(nlohmann::json{
            {"period", period.period},
            {"income", period.income},
            {"expense", period.expense}});
    }
    return HttpResponseMapper::json(200, nlohmann::json{
        {"baseCurrency", result->base_currency},
        {"trends", std::move(trends)}});
}

HttpResponse ReportController::dashboard_summary(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (request.query.contains("startDate") || request.query.contains("endDate")) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "Dashboard summary uses the current month from user preferences"),
            request.trace_id);
    }
    auto result = service_.dashboard_summary(*user);
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);

    nlohmann::json distribution = nlohmann::json::array();
    for (const auto& slice : result->asset_distribution) {
        distribution.push_back(nlohmann::json{
            {"label", slice.label},
            {"amount", slice.amount},
            {"percentage", slice.percentage}});
    }
    nlohmann::json categories = nlohmann::json::array();
    for (const auto& category : result->top_expense_categories) {
        categories.push_back(nlohmann::json{
            {"categoryId", category.category_id.has_value()
                ? nlohmann::json(category.category_id->value())
                : nlohmann::json(nullptr)},
            {"categoryName", category.category_name},
            {"amount", category.amount},
            {"percentage", category.percentage}});
    }
    return HttpResponseMapper::json(200, nlohmann::json{
        {"baseCurrency", result->currency_code},
        {"netWorth", nlohmann::json{
            {"baseCurrency", result->currency_code},
            {"totalAssets", result->total_assets},
            {"totalLiabilities", result->total_liabilities},
            {"netWorth", result->net_worth},
            {"generatedAt", TimeCodec::format_rfc3339(result->generated_at)}}},
        {"monthlyIncome", result->income_total},
        {"monthlyExpense", result->expense_total},
        {"assetDistribution", std::move(distribution)},
        {"topExpenseCategories", std::move(categories)},
        {"reportPeriodStart", TimeCodec::format_rfc3339(
            result->report_period_start)},
        {"reportPeriodEnd", TimeCodec::format_rfc3339(
            result->report_period_end)},
        {"generatedAt", TimeCodec::format_rfc3339(result->generated_at)}});
}

} // namespace pfh::presentation
