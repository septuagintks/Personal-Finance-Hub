// Personal Finance Hub - Report Controller

#include "pfh/presentation/controllers/report_controller.h"

#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/json_request_parser.h"
#include "pfh/presentation/http/time_codec.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
#include <set>
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

[[nodiscard]] application::Result<application::ReportDimension> parse_dimension(
    std::string_view value) {
    if (value == "root_category") {
        return application::ReportDimension::RootCategory;
    }
    if (value == "account") return application::ReportDimension::Account;
    if (value == "tag") return application::ReportDimension::Tag;
    return application::err(application::Error::validation(
        "dimension must be root_category, account, or tag"));
}

[[nodiscard]] std::string dimension_text(
    application::ReportDimension value) {
    switch (value) {
    case application::ReportDimension::RootCategory: return "root_category";
    case application::ReportDimension::Account: return "account";
    case application::ReportDimension::Tag: return "tag";
    }
    return "root_category";
}

[[nodiscard]] std::string rate_status_text(
    application::ReportRateStatus value) {
    return value == application::ReportRateStatus::Historical
        ? "historical" : "current";
}

[[nodiscard]] application::Result<domain::TransactionType> parse_type(
    const std::string& value) {
    if (value == "income") return domain::TransactionType::Income;
    if (value == "expense") return domain::TransactionType::Expense;
    if (value == "transfer") return domain::TransactionType::Transfer;
    if (value == "adjustment") return domain::TransactionType::Adjustment;
    return application::err(application::Error::validation(
        "type must be income, expense, transfer, or adjustment"));
}

template <typename TypedId>
[[nodiscard]] application::Result<std::optional<TypedId>> optional_query_id(
    const HttpRequest& request,
    std::string_view name) {
    const auto found = request.query.find(std::string(name));
    if (found == request.query.end()) return std::optional<TypedId>{};
    auto id = JsonRequestParser::path_id<TypedId>(found->second, name);
    if (!id) return application::err(id.error());
    return std::optional<TypedId>(*id);
}

[[nodiscard]] application::Result<std::optional<
    std::chrono::system_clock::time_point>> optional_query_time(
    const HttpRequest& request,
    std::string_view name) {
    const auto found = request.query.find(std::string(name));
    if (found == request.query.end()) {
        return std::optional<std::chrono::system_clock::time_point>{};
    }
    auto value = TimeCodec::parse_rfc3339(found->second);
    if (!value) return application::err(value.error());
    return std::optional<std::chrono::system_clock::time_point>(*value);
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

HttpResponse ReportController::analysis(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    static const std::set<std::string> allowed{
        "startDate", "endDate", "dimension"};
    for (const auto& [name, _] : request.query) {
        if (!allowed.contains(name)) {
            return HttpResponseMapper::error(
                application::Error::validation(
                    "Unknown query parameter: " + name),
                request.trace_id);
        }
    }
    const auto start_value = request.query.find("startDate");
    const auto end_value = request.query.find("endDate");
    const auto dimension_value = request.query.find("dimension");
    if (start_value == request.query.end() ||
        end_value == request.query.end() ||
        dimension_value == request.query.end()) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "startDate, endDate, and dimension are required"),
            request.trace_id);
    }
    auto start = parse_month(start_value->second, "startDate");
    auto end = parse_month(end_value->second, "endDate");
    auto dimension = parse_dimension(dimension_value->second);
    if (!start) return HttpResponseMapper::error(start.error(), request.trace_id);
    if (!end) return HttpResponseMapper::error(end.error(), request.trace_id);
    if (!dimension) {
        return HttpResponseMapper::error(dimension.error(), request.trace_id);
    }

    auto result = service_.report_analysis(application::ReportAnalysisQuery{
        *user, start->year, start->month, end->year, end->month, *dimension});
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);

    nlohmann::json trend = nlohmann::json::array();
    for (const auto& point : result->net_worth_trend) {
        trend.push_back(nlohmann::json{
            {"period", point.period},
            {"totalAssets", point.total_assets},
            {"totalLiabilities", point.total_liabilities},
            {"netWorth", point.net_worth},
            {"valuedAt", TimeCodec::format_rfc3339(point.valued_at)},
            {"rateStatus", rate_status_text(point.rate_status)}});
    }
    nlohmann::json breakdown = nlohmann::json::array();
    for (const auto& slice : result->breakdown) {
        breakdown.push_back(nlohmann::json{
            {"key", slice.key},
            {"label", slice.label},
            {"income", slice.income},
            {"expense", slice.expense},
            {"net", slice.net}});
    }
    return HttpResponseMapper::json(200, nlohmann::json{
        {"baseCurrency", result->base_currency},
        {"valuationAt", TimeCodec::format_rfc3339(result->valuation_at)},
        {"rateStatus", rate_status_text(result->rate_status)},
        {"reportPeriodStart", TimeCodec::format_rfc3339(
            result->report_period_start)},
        {"reportPeriodEnd", TimeCodec::format_rfc3339(
            result->report_period_end)},
        {"dimension", dimension_text(result->dimension)},
        {"dimensionOverlaps", result->dimension_overlaps},
        {"netWorthTrend", std::move(trend)},
        {"breakdown", std::move(breakdown)}});
}

HttpResponse ReportController::export_transactions(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    static const std::set<std::string> allowed{
        "accountId", "type", "categoryId", "tagId", "from", "to",
        "keyword"};
    for (const auto& [name, _] : request.query) {
        if (!allowed.contains(name)) {
            return HttpResponseMapper::error(
                application::Error::validation(
                    "Unknown query parameter: " + name),
                request.trace_id);
        }
    }
    auto account = optional_query_id<domain::AccountId>(request, "accountId");
    auto category = optional_query_id<domain::CategoryId>(request, "categoryId");
    auto tag = optional_query_id<domain::TagId>(request, "tagId");
    auto from = optional_query_time(request, "from");
    auto to = optional_query_time(request, "to");
    if (!account) return HttpResponseMapper::error(account.error(), request.trace_id);
    if (!category) return HttpResponseMapper::error(category.error(), request.trace_id);
    if (!tag) return HttpResponseMapper::error(tag.error(), request.trace_id);
    if (!from) return HttpResponseMapper::error(from.error(), request.trace_id);
    if (!to) return HttpResponseMapper::error(to.error(), request.trace_id);

    std::optional<domain::TransactionType> type;
    if (const auto found = request.query.find("type");
        found != request.query.end()) {
        auto parsed = parse_type(found->second);
        if (!parsed) return HttpResponseMapper::error(parsed.error(), request.trace_id);
        type = *parsed;
    }
    application::TransactionListQuery query;
    query.user_id = *user;
    query.account_id = *account;
    query.type = type;
    query.category_id = *category;
    query.tag_id = *tag;
    query.occurred_from = *from;
    query.occurred_to = *to;
    if (const auto found = request.query.find("keyword");
        found != request.query.end()) {
        query.keyword = found->second;
    }
    auto result = service_.export_transactions_csv(query);
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);

    HttpResponse response;
    response.status = 200;
    response.headers.emplace("Content-Type", "text/csv; charset=utf-8");
    response.headers.emplace(
        "Content-Disposition",
        "attachment; filename=\"" + result->filename + "\"");
    response.headers.emplace(
        "X-Export-Row-Count", std::to_string(result->row_count));
    response.body = std::move(result->content);
    return response;
}

} // namespace pfh::presentation
