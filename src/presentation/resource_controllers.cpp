// Personal Finance Hub - Foundational Resource Controllers

#include "pfh/presentation/controllers/resource_controllers.h"

#include "pfh/application/use_cases/delete_account_use_case.h"
#include "pfh/presentation/http/concurrency.h"
#include "pfh/presentation/http/http_response_mapper.h"
#include "pfh/presentation/http/json_request_parser.h"
#include "pfh/presentation/http/time_codec.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace pfh::presentation {

namespace {

using Json = nlohmann::json;

void mix_catalog_text(std::uint64_t& hash, std::string_view value) {
    const auto mix_byte = [&](std::uint8_t byte) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    };
    auto size = static_cast<std::uint64_t>(value.size());
    for (int shift = 0; shift < 64; shift += 8) {
        mix_byte(static_cast<std::uint8_t>(size >> shift));
    }
    for (const char value_byte : value) {
        mix_byte(static_cast<std::uint8_t>(
            static_cast<unsigned char>(value_byte)));
    }
}

[[nodiscard]] std::string catalog_etag(
    std::string_view catalog,
    std::uint64_t hash) {
    std::ostringstream result;
    result << "W/\"pfh-" << catalog << '-' << std::hex << std::setfill('0')
           << std::setw(16) << hash << '\"';
    return result.str();
}

[[nodiscard]] std::string currency_catalog_etag(
    const std::vector<application::CurrencyMetadataDto>& currencies) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& currency : currencies) {
        mix_catalog_text(hash, currency.code);
        mix_catalog_text(hash, currency.symbol);
        mix_catalog_text(hash, std::to_string(currency.precision));
        mix_catalog_text(hash, currency.display_name);
        mix_catalog_text(hash, currency.is_crypto ? "1" : "0");
    }
    return catalog_etag("currency", hash);
}

[[nodiscard]] std::string timezone_catalog_etag(
    const std::vector<application::TimeZoneMetadataDto>& timezones) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& timezone : timezones) {
        mix_catalog_text(hash, timezone.id);
        mix_catalog_text(hash, timezone.canonical_id);
        mix_catalog_text(hash, timezone.is_alias ? "1" : "0");
    }
    return catalog_etag("timezone", hash);
}

[[nodiscard]] application::Result<domain::UserId> require_user(
    const HttpRequest& request) {
    if (!request.identity.has_value() ||
        !request.identity->access_claims.user_id.is_valid()) {
        return application::err(application::Error::unauthorized());
    }
    return request.identity->access_claims.user_id;
}

[[nodiscard]] std::string account_type_text(domain::AccountType type) {
    switch (type) {
    case domain::AccountType::Cash: return "cash";
    case domain::AccountType::Savings: return "savings";
    case domain::AccountType::Credit: return "credit";
    case domain::AccountType::DigitalWallet: return "digital_wallet";
    case domain::AccountType::Investment: return "investment";
    case domain::AccountType::Crypto: return "crypto";
    case domain::AccountType::Other: return "other";
    }
    return "other";
}

[[nodiscard]] application::Result<domain::AccountType> parse_account_type(
    const std::string& value) {
    if (value == "cash") return domain::AccountType::Cash;
    if (value == "savings") return domain::AccountType::Savings;
    if (value == "credit") return domain::AccountType::Credit;
    if (value == "digital_wallet") return domain::AccountType::DigitalWallet;
    if (value == "investment") return domain::AccountType::Investment;
    if (value == "crypto") return domain::AccountType::Crypto;
    if (value == "other") return domain::AccountType::Other;
    return application::err(application::Error::validation(
        "type is not a supported account type"));
}

[[nodiscard]] std::string account_category_text(domain::AccountCategory value) {
    return value == domain::AccountCategory::Asset ? "asset" : "liability";
}

[[nodiscard]] application::Result<domain::AccountCategory> parse_account_category(
    const std::string& value) {
    if (value == "asset") return domain::AccountCategory::Asset;
    if (value == "liability") return domain::AccountCategory::Liability;
    return application::err(application::Error::validation(
        "category must be asset or liability"));
}

[[nodiscard]] std::string board_text(domain::CategoryBoard value) {
    return value == domain::CategoryBoard::Income ? "income" : "expense";
}

[[nodiscard]] application::Result<domain::CategoryBoard> parse_board(
    const std::string& value) {
    if (value == "income") return domain::CategoryBoard::Income;
    if (value == "expense") return domain::CategoryBoard::Expense;
    return application::err(application::Error::validation(
        "board must be income or expense"));
}

[[nodiscard]] application::Result<application::MetadataListStatus>
parse_metadata_status(const std::string& value) {
    if (value == "active") return application::MetadataListStatus::Active;
    if (value == "deleted") return application::MetadataListStatus::Deleted;
    if (value == "all") return application::MetadataListStatus::All;
    return application::err(application::Error::validation(
        "status must be active, deleted, or all"));
}

[[nodiscard]] std::string source_text(domain::CategorySource value) {
    return value == domain::CategorySource::System ? "system" : "user";
}

[[nodiscard]] Json account_json(const application::AccountDto& value) {
    return Json{
        {"id", value.id.value()},
        {"name", value.name},
        {"type", account_type_text(value.type)},
        {"subtype", value.subtype},
        {"category", account_category_text(value.category)},
        {"currencyCode", value.currency_code},
        {"description", value.description},
        {"isArchived", value.is_archived},
        {"archivedAt", value.archived_at.has_value()
            ? Json(TimeCodec::format_rfc3339(*value.archived_at)) : Json(nullptr)},
        {"createdAt", TimeCodec::format_rfc3339(value.created_at)},
        {"updatedAt", TimeCodec::format_rfc3339(value.updated_at)},
        {"version", value.version}};
}

[[nodiscard]] Json category_json(const application::CategoryDto& value) {
    Json result{
        {"id", value.id.value()},
        {"name", value.name},
        {"board", board_text(value.board)},
        {"source", source_text(value.source)},
        {"parentId", value.parent_id.has_value()
            ? Json(value.parent_id->value()) : Json(nullptr)},
        {"templateId", value.template_id.has_value()
            ? Json(*value.template_id) : Json(nullptr)},
        {"sortOrder", value.sort_order},
        {"isDeleted", value.is_deleted},
        {"deletedAt", value.deleted_at.has_value()
            ? Json(TimeCodec::format_rfc3339(*value.deleted_at)) : Json(nullptr)},
        {"createdAt", TimeCodec::format_rfc3339(value.created_at)},
        {"updatedAt", TimeCodec::format_rfc3339(value.updated_at)}};
    return result;
}

[[nodiscard]] Json category_tree_json(
    const application::CategoryTreeDto& value) {
    auto result = category_json(value);
    result["children"] = Json::array();
    for (const auto& child : value.children) {
        result["children"].push_back(category_tree_json(child));
    }
    return result;
}

[[nodiscard]] Json tag_json(const application::TagDto& value) {
    return Json{
        {"id", value.id.value()},
        {"name", value.name},
        {"isDeleted", value.is_deleted},
        {"deletedAt", value.deleted_at.has_value()
            ? Json(TimeCodec::format_rfc3339(*value.deleted_at)) : Json(nullptr)},
        {"createdAt", TimeCodec::format_rfc3339(value.created_at)},
        {"updatedAt", TimeCodec::format_rfc3339(value.updated_at)}};
}

[[nodiscard]] std::string theme_text(domain::ThemeMode value) {
    switch (value) {
    case domain::ThemeMode::System: return "system";
    case domain::ThemeMode::Light: return "light";
    case domain::ThemeMode::Dark: return "dark";
    }
    return "system";
}

[[nodiscard]] std::string home_page_text(domain::HomePage value) {
    switch (value) {
    case domain::HomePage::Dashboard: return "dashboard";
    case domain::HomePage::Transactions: return "transactions";
    case domain::HomePage::Reports: return "reports";
    case domain::HomePage::Accounts: return "accounts";
    }
    return "dashboard";
}

[[nodiscard]] std::string report_period_text(domain::ReportPeriod value) {
    switch (value) {
    case domain::ReportPeriod::CurrentMonth: return "current_month";
    case domain::ReportPeriod::LastMonth: return "last_month";
    case domain::ReportPeriod::Last3Months: return "last_3_months";
    case domain::ReportPeriod::CurrentYear: return "current_year";
    case domain::ReportPeriod::Custom: return "custom";
    }
    return "current_month";
}

[[nodiscard]] std::string number_format_text(domain::NumberFormat value) {
    switch (value) {
    case domain::NumberFormat::CommaDot: return "1,234.56";
    case domain::NumberFormat::DotComma: return "1.234,56";
    case domain::NumberFormat::SpaceComma: return "1 234,56";
    }
    return "1,234.56";
}

[[nodiscard]] Json report_month_json(
    std::optional<domain::ReportMonth> value) {
    if (!value.has_value()) return nullptr;
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << int(value->year())
           << '-' << std::setw(2) << unsigned(value->month());
    return output.str();
}

[[nodiscard]] Json preference_json(const application::UserPreferenceDto& value) {
    return Json{
        {"baseCurrency", value.base_currency},
        {"locale", value.locale},
        {"timezone", value.timezone},
        {"dateFormat", value.date_format},
        {"numberFormat", number_format_text(value.number_format)},
        {"theme", theme_text(value.theme)},
        {"defaultHomePage", home_page_text(value.default_home_page)},
        {"defaultReportPeriod", report_period_text(value.default_report_period)},
        {"customReportStartMonth", report_month_json(value.custom_report_start_month)},
        {"customReportEndMonth", report_month_json(value.custom_report_end_month)}};
}

template <typename Enum>
[[nodiscard]] application::Result<Enum> invalid_enum(const std::string& field) {
    return application::err(application::Error::validation(
        field + " has an unsupported value"));
}

[[nodiscard]] application::Result<domain::ThemeMode> parse_theme(
    const std::string& value) {
    if (value == "system") return domain::ThemeMode::System;
    if (value == "light") return domain::ThemeMode::Light;
    if (value == "dark") return domain::ThemeMode::Dark;
    return invalid_enum<domain::ThemeMode>("theme");
}

[[nodiscard]] application::Result<domain::HomePage> parse_home_page(
    const std::string& value) {
    if (value == "dashboard") return domain::HomePage::Dashboard;
    if (value == "transactions") return domain::HomePage::Transactions;
    if (value == "reports") return domain::HomePage::Reports;
    if (value == "accounts") return domain::HomePage::Accounts;
    return invalid_enum<domain::HomePage>("defaultHomePage");
}

[[nodiscard]] application::Result<domain::ReportPeriod> parse_report_period(
    const std::string& value) {
    if (value == "current_month") return domain::ReportPeriod::CurrentMonth;
    if (value == "last_month") return domain::ReportPeriod::LastMonth;
    if (value == "last_3_months") return domain::ReportPeriod::Last3Months;
    if (value == "current_year") return domain::ReportPeriod::CurrentYear;
    if (value == "custom") return domain::ReportPeriod::Custom;
    return invalid_enum<domain::ReportPeriod>("defaultReportPeriod");
}

[[nodiscard]] application::Result<domain::NumberFormat> parse_number_format(
    const std::string& value) {
    if (value == "1,234.56") return domain::NumberFormat::CommaDot;
    if (value == "1.234,56") return domain::NumberFormat::DotComma;
    if (value == "1 234,56") return domain::NumberFormat::SpaceComma;
    return invalid_enum<domain::NumberFormat>("numberFormat");
}

[[nodiscard]] application::Result<std::optional<domain::ReportMonth>>
parse_report_month(
    const std::optional<std::string>& value,
    std::string_view field) {
    if (!value.has_value()) {
        return std::optional<domain::ReportMonth>{};
    }
    int year = 0;
    unsigned month = 0;
    const auto& text = *value;
    if (text.size() != 7 || text[4] != '-') {
        return application::err(application::Error::validation(
            std::string(field) + " must use YYYY-MM"));
    }
    const auto year_result = std::from_chars(text.data(), text.data() + 4, year);
    const auto month_result = std::from_chars(
        text.data() + 5, text.data() + text.size(), month);
    const domain::ReportMonth parsed{
        std::chrono::year{year}, std::chrono::month{month}};
    if (year_result.ec != std::errc{} || year_result.ptr != text.data() + 4 ||
        month_result.ec != std::errc{} ||
        month_result.ptr != text.data() + text.size() || year < 1 ||
        !parsed.ok()) {
        return application::err(application::Error::validation(
            std::string(field) + " must use a valid YYYY-MM"));
    }
    return std::optional<domain::ReportMonth>(parsed);
}

[[nodiscard]] application::Result<int> parse_confirmations(
    const HttpRequest& request) {
    const auto found = request.query.find("confirmations");
    if (found == request.query.end()) {
        return application::err(application::Error::validation(
            "confirmations query parameter is required"));
    }
    int value = 0;
    const auto result = std::from_chars(
        found->second.data(), found->second.data() + found->second.size(), value);
    if (result.ec != std::errc{} ||
        result.ptr != found->second.data() + found->second.size()) {
        return application::err(application::Error::validation(
            "confirmations must be an integer"));
    }
    return value;
}

} // namespace

HttpResponse AccountController::list(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    application::AccountListStatus status = application::AccountListStatus::Active;
    if (const auto found = request.query.find("status"); found != request.query.end()) {
        if (found->second == "archived") {
            status = application::AccountListStatus::Archived;
        } else if (found->second == "all") {
            status = application::AccountListStatus::All;
        } else if (found->second != "active") {
            return HttpResponseMapper::error(
                application::Error::validation(
                    "status must be active, archived, or all"),
                request.trace_id);
        }
    }
    auto result = service_.list_accounts(*user, status);
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    Json body = Json::array();
    for (const auto& account : *result) body.push_back(account_json(account));
    return HttpResponseMapper::json(200, body);
}

HttpResponse AccountController::create(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    const auto idempotency_key = request.header("Idempotency-Key");
    if (!idempotency_key.has_value()) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "Idempotency-Key header is required"),
            request.trace_id);
    }
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body, {"name", "type", "subtype", "category",
                    "currencyCode", "description"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto name = JsonRequestParser::required_string(*body, "name", 128);
    auto type_text = JsonRequestParser::required_string(*body, "type", 32);
    auto subtype = JsonRequestParser::required_string(*body, "subtype", 64);
    auto currency = JsonRequestParser::required_string(*body, "currencyCode", 10);
    auto description = JsonRequestParser::optional_string_allow_empty(
        *body, "description", 4096);
    auto category_text = JsonRequestParser::optional_string(*body, "category", 16);
    if (!name) return HttpResponseMapper::error(name.error(), request.trace_id);
    if (!type_text) return HttpResponseMapper::error(type_text.error(), request.trace_id);
    if (!subtype) return HttpResponseMapper::error(subtype.error(), request.trace_id);
    if (!currency) return HttpResponseMapper::error(currency.error(), request.trace_id);
    if (!description) return HttpResponseMapper::error(description.error(), request.trace_id);
    if (!category_text) return HttpResponseMapper::error(category_text.error(), request.trace_id);
    auto type = parse_account_type(*type_text);
    if (!type) return HttpResponseMapper::error(type.error(), request.trace_id);
    std::optional<domain::AccountCategory> category;
    if (category_text->has_value()) {
        auto parsed = parse_account_category(**category_text);
        if (!parsed) return HttpResponseMapper::error(parsed.error(), request.trace_id);
        category = *parsed;
    }

    auto result = service_.create_account(application::CreateAccountCommand{
        *user, *name, *type, *subtype, *currency,
        description->value_or(""), category}, *idempotency_key);
    return result
        ? HttpResponseMapper::json(201, account_json(*result))
        : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse AccountController::get(
    const HttpRequest& request, std::string_view account_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::AccountId>(account_id, "accountId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.get_account(*user, *id);
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    auto response = HttpResponseMapper::json(200, account_json(*result));
    response.headers.emplace("ETag", version_etag(result->version));
    return response;
}

HttpResponse AccountController::update(
    const HttpRequest& request, std::string_view account_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::AccountId>(account_id, "accountId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto version = parse_if_match_version(request.header("If-Match").value_or(""));
    if (!version) return HttpResponseMapper::error(version.error(), request.trace_id);
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body, {"name", "type", "subtype", "category",
                    "currencyCode", "description"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto name = JsonRequestParser::required_string(*body, "name", 128);
    auto type_text = JsonRequestParser::required_string(*body, "type", 32);
    auto subtype = JsonRequestParser::required_string(*body, "subtype", 64);
    auto category_text = JsonRequestParser::required_string(*body, "category", 16);
    auto currency = JsonRequestParser::required_string(*body, "currencyCode", 10);
    auto description = JsonRequestParser::required_string_allow_empty(
        *body, "description", 4096);
    if (!name) return HttpResponseMapper::error(name.error(), request.trace_id);
    if (!type_text) return HttpResponseMapper::error(type_text.error(), request.trace_id);
    if (!subtype) return HttpResponseMapper::error(subtype.error(), request.trace_id);
    if (!category_text) return HttpResponseMapper::error(category_text.error(), request.trace_id);
    if (!currency) return HttpResponseMapper::error(currency.error(), request.trace_id);
    if (!description) return HttpResponseMapper::error(description.error(), request.trace_id);
    auto type = parse_account_type(*type_text);
    auto category = parse_account_category(*category_text);
    if (!type) return HttpResponseMapper::error(type.error(), request.trace_id);
    if (!category) return HttpResponseMapper::error(category.error(), request.trace_id);
    auto result = service_.update_account(application::UpdateAccountCommand{
        *user, *id, *version, *name, *type, *subtype, *category, *currency,
        *description});
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    auto response = HttpResponseMapper::json(200, account_json(*result));
    response.headers.emplace("ETag", version_etag(result->version));
    return response;
}

HttpResponse AccountController::balance(
    const HttpRequest& request, std::string_view account_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::AccountId>(account_id, "accountId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.account_balance(*user, *id);
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    return HttpResponseMapper::json(200, Json{
        {"accountId", result->account_id.value()},
        {"currencyCode", result->currency_code},
        {"balance", result->amount},
        {"lastTransactionId", result->last_transaction_id.has_value()
            ? Json(result->last_transaction_id->value()) : Json(nullptr)},
        {"updatedAt", TimeCodec::format_rfc3339(result->updated_at)}});
}

HttpResponse AccountController::archive(
    const HttpRequest& request, std::string_view account_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::AccountId>(account_id, "accountId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto version = parse_if_match_version(request.header("If-Match").value_or(""));
    if (!version) return HttpResponseMapper::error(version.error(), request.trace_id);
    auto result = service_.archive_account(
        application::ArchiveAccountCommand{*user, *id, *version, std::nullopt});
    return result ? HttpResponseMapper::no_content()
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse AccountController::restore(
    const HttpRequest& request, std::string_view account_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::AccountId>(account_id, "accountId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto version = parse_if_match_version(request.header("If-Match").value_or(""));
    if (!version) return HttpResponseMapper::error(version.error(), request.trace_id);
    auto result = service_.restore_account(
        application::RestoreAccountCommand{*user, *id, *version, std::nullopt});
    return result ? HttpResponseMapper::no_content()
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse AccountController::dangerous_delete(
    const HttpRequest& request, std::string_view account_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::AccountId>(account_id, "accountId");
    auto confirmations = parse_confirmations(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    if (!confirmations) {
        return HttpResponseMapper::error(confirmations.error(), request.trace_id);
    }
    auto result = service_.delete_account(
        application::DeleteAccountCommand{*user, *id, *confirmations});
    return result ? HttpResponseMapper::no_content()
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse CategoryController::list(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    std::optional<domain::CategoryBoard> board;
    auto status = application::MetadataListStatus::Active;
    if (const auto found = request.query.find("board"); found != request.query.end()) {
        auto parsed = parse_board(found->second);
        if (!parsed) return HttpResponseMapper::error(parsed.error(), request.trace_id);
        board = *parsed;
    }
    if (const auto found = request.query.find("status"); found != request.query.end()) {
        auto parsed = parse_metadata_status(found->second);
        if (!parsed) return HttpResponseMapper::error(parsed.error(), request.trace_id);
        status = *parsed;
    }
    auto result = service_.list_categories(*user, board, status);
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    Json body = Json::array();
    for (const auto& category : *result) {
        body.push_back(category_tree_json(category));
    }
    return HttpResponseMapper::json(200, body);
}

HttpResponse CategoryController::update(
    const HttpRequest& request, std::string_view category_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::CategoryId>(category_id, "categoryId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body, {"name", "sortOrder"}); !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto name = JsonRequestParser::required_string(*body, "name", 128);
    if (!name) return HttpResponseMapper::error(name.error(), request.trace_id);
    const auto sort = body->find("sortOrder");
    if (sort == body->end() || !sort->is_number_integer()) {
        return HttpResponseMapper::error(
            application::Error::validation("sortOrder must be an integer"),
            request.trace_id);
    }
    const auto raw_sort = sort->get<std::int64_t>();
    if (raw_sort < std::numeric_limits<int>::min() ||
        raw_sort > std::numeric_limits<int>::max()) {
        return HttpResponseMapper::error(
            application::Error::validation("sortOrder is outside int32 range"),
            request.trace_id);
    }
    auto result = service_.update_category(application::UpdateCategoryCommand{
        *user, *id, *name, static_cast<int>(raw_sort)});
    return result ? HttpResponseMapper::json(200, category_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse CategoryController::restore(
    const HttpRequest& request, std::string_view category_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::CategoryId>(category_id, "categoryId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.restore_category(
        application::RestoreCategoryCommand{*user, *id});
    return result ? HttpResponseMapper::json(200, category_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse CategoryController::create(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    const auto idempotency_key = request.header("Idempotency-Key");
    if (!idempotency_key.has_value()) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "Idempotency-Key header is required"),
            request.trace_id);
    }
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body, {"board", "name", "parentId", "templateId"}); !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto board_text_value = JsonRequestParser::optional_string(*body, "board", 16);
    auto name = JsonRequestParser::optional_string(*body, "name", 128);
    auto parent = JsonRequestParser::optional_id<domain::CategoryId>(*body, "parentId");
    if (!board_text_value) return HttpResponseMapper::error(board_text_value.error(), request.trace_id);
    if (!name) return HttpResponseMapper::error(name.error(), request.trace_id);
    if (!parent) return HttpResponseMapper::error(parent.error(), request.trace_id);
    std::optional<domain::CategoryBoard> board;
    if (board_text_value->has_value()) {
        auto parsed = parse_board(**board_text_value);
        if (!parsed) return HttpResponseMapper::error(parsed.error(), request.trace_id);
        board = *parsed;
    }
    std::optional<std::int64_t> template_id;
    if (const auto found = body->find("templateId");
        found != body->end() && !found->is_null()) {
        auto value = JsonRequestParser::positive_integer(*found, "templateId");
        if (!value) return HttpResponseMapper::error(value.error(), request.trace_id);
        template_id = *value;
    }
    auto result = service_.create_category(application::CreateCategoryCommand{
        *user, board, *name, *parent, template_id}, *idempotency_key);
    return result
        ? HttpResponseMapper::json(201, category_json(*result))
        : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse CategoryController::remove(
    const HttpRequest& request, std::string_view category_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::CategoryId>(category_id, "categoryId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.delete_category(
        application::DeleteCategoryCommand{*user, *id, std::nullopt});
    return result ? HttpResponseMapper::no_content()
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TagController::list(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    auto status = application::MetadataListStatus::Active;
    if (const auto found = request.query.find("status"); found != request.query.end()) {
        auto parsed = parse_metadata_status(found->second);
        if (!parsed) return HttpResponseMapper::error(parsed.error(), request.trace_id);
        status = *parsed;
    }
    auto result = service_.list_tags(*user, status);
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    Json body = Json::array();
    for (const auto& tag : *result) body.push_back(tag_json(tag));
    return HttpResponseMapper::json(200, body);
}

HttpResponse TagController::update(
    const HttpRequest& request, std::string_view tag_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TagId>(tag_id, "tagId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(*body, {"name"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto name = JsonRequestParser::required_string(*body, "name", 64);
    if (!name) return HttpResponseMapper::error(name.error(), request.trace_id);
    auto result = service_.update_tag(
        application::UpdateTagCommand{*user, *id, *name});
    return result ? HttpResponseMapper::json(200, tag_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TagController::restore(
    const HttpRequest& request, std::string_view tag_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TagId>(tag_id, "tagId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.restore_tag(application::RestoreTagCommand{*user, *id});
    return result ? HttpResponseMapper::json(200, tag_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TagController::create(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    const auto idempotency_key = request.header("Idempotency-Key");
    if (!idempotency_key.has_value()) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "Idempotency-Key header is required"),
            request.trace_id);
    }
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(*body, {"name"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    auto name = JsonRequestParser::required_string(*body, "name", 64);
    if (!name) return HttpResponseMapper::error(name.error(), request.trace_id);
    auto result = service_.create_tag(
        application::CreateTagCommand{*user, *name}, *idempotency_key);
    return result ? HttpResponseMapper::json(201, tag_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TagController::remove(
    const HttpRequest& request, std::string_view tag_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TagId>(tag_id, "tagId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto result = service_.delete_tag(
        application::DeleteTagCommand{*user, *id, std::nullopt});
    return result ? HttpResponseMapper::no_content()
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse TagController::replace_transaction_tags(
    const HttpRequest& request, std::string_view transaction_id) {
    auto user = require_user(request);
    auto id = JsonRequestParser::path_id<domain::TransactionId>(
        transaction_id, "transactionId");
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    if (!id) return HttpResponseMapper::error(id.error(), request.trace_id);
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(*body, {"tagIds"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    const auto found = body->find("tagIds");
    if (found == body->end() || !found->is_array()) {
        return HttpResponseMapper::error(
            application::Error::validation("tagIds must be an array"),
            request.trace_id);
    }
    if (found->size() > domain::kMaxTagsPerTransaction) {
        return HttpResponseMapper::error(
            application::Error::validation(
                "tagIds must contain at most 64 items"),
            request.trace_id);
    }
    std::vector<domain::TagId> tag_ids;
    tag_ids.reserve(found->size());
    for (const auto& item : *found) {
        auto value = JsonRequestParser::positive_integer(item, "tagIds[]");
        if (!value) return HttpResponseMapper::error(value.error(), request.trace_id);
        tag_ids.push_back(domain::TagId(*value));
    }
    auto result = service_.replace_transaction_tags(
        application::ReplaceTransactionTagsCommand{*user, *id, std::move(tag_ids)});
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    Json response = Json::array();
    for (const auto& tag : *result) response.push_back(tag_json(tag));
    return HttpResponseMapper::json(200, response);
}

HttpResponse PreferenceController::get(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    auto result = service_.get_preferences(*user);
    return result ? HttpResponseMapper::json(200, preference_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse PreferenceController::update(const HttpRequest& request) {
    auto user = require_user(request);
    if (!user) return HttpResponseMapper::error(user.error(), request.trace_id);
    auto body = JsonRequestParser::parse_object(request);
    if (!body) return HttpResponseMapper::error(body.error(), request.trace_id);
    if (auto fields = JsonRequestParser::reject_unknown_fields(
            *body,
            {"baseCurrency", "locale", "timezone", "dateFormat", "numberFormat",
             "theme", "defaultHomePage", "defaultReportPeriod",
             "customReportStartMonth", "customReportEndMonth"});
        !fields) {
        return HttpResponseMapper::error(fields.error(), request.trace_id);
    }
    for (const std::string_view field : {
             "customReportStartMonth", "customReportEndMonth"}) {
        if (!body->contains(std::string(field))) {
            return HttpResponseMapper::error(
                application::Error(
                    application::ErrorCode::MissingRequiredField,
                    std::string(field) + " is required"),
                request.trace_id);
        }
    }
    auto base = JsonRequestParser::required_string(*body, "baseCurrency", 10);
    auto locale = JsonRequestParser::required_string(*body, "locale", 16);
    auto timezone = JsonRequestParser::required_string(*body, "timezone", 64);
    auto date = JsonRequestParser::required_string(*body, "dateFormat", 32);
    auto number = JsonRequestParser::required_string(*body, "numberFormat", 32);
    auto theme_value = JsonRequestParser::required_string(*body, "theme", 16);
    auto home_value = JsonRequestParser::required_string(*body, "defaultHomePage", 32);
    auto period_value = JsonRequestParser::required_string(
        *body, "defaultReportPeriod", 32);
    auto custom_start_value = JsonRequestParser::optional_string(
        *body, "customReportStartMonth", 7);
    auto custom_end_value = JsonRequestParser::optional_string(
        *body, "customReportEndMonth", 7);
    if (!base) return HttpResponseMapper::error(base.error(), request.trace_id);
    if (!locale) return HttpResponseMapper::error(locale.error(), request.trace_id);
    if (!timezone) return HttpResponseMapper::error(timezone.error(), request.trace_id);
    if (!date) return HttpResponseMapper::error(date.error(), request.trace_id);
    if (!number) return HttpResponseMapper::error(number.error(), request.trace_id);
    if (!theme_value) return HttpResponseMapper::error(theme_value.error(), request.trace_id);
    if (!home_value) return HttpResponseMapper::error(home_value.error(), request.trace_id);
    if (!period_value) return HttpResponseMapper::error(period_value.error(), request.trace_id);
    if (!custom_start_value) return HttpResponseMapper::error(custom_start_value.error(), request.trace_id);
    if (!custom_end_value) return HttpResponseMapper::error(custom_end_value.error(), request.trace_id);
    auto theme = parse_theme(*theme_value);
    auto home = parse_home_page(*home_value);
    auto period = parse_report_period(*period_value);
    auto number_format = parse_number_format(*number);
    auto custom_start = parse_report_month(*custom_start_value, "customReportStartMonth");
    auto custom_end = parse_report_month(*custom_end_value, "customReportEndMonth");
    if (!theme) return HttpResponseMapper::error(theme.error(), request.trace_id);
    if (!home) return HttpResponseMapper::error(home.error(), request.trace_id);
    if (!period) return HttpResponseMapper::error(period.error(), request.trace_id);
    if (!number_format) return HttpResponseMapper::error(number_format.error(), request.trace_id);
    if (!custom_start) return HttpResponseMapper::error(custom_start.error(), request.trace_id);
    if (!custom_end) return HttpResponseMapper::error(custom_end.error(), request.trace_id);
    auto result = service_.update_preferences(application::UpdateUserPreferenceCommand{
        *user, *base, *locale, *timezone, *date, *number_format, *theme, *home,
        *period, *custom_start, *custom_end});
    return result ? HttpResponseMapper::json(200, preference_json(*result))
                  : HttpResponseMapper::error(result.error(), request.trace_id);
}

HttpResponse CurrencyController::list(const HttpRequest& request) {
    auto result = service_.list_currencies();
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    const auto etag = currency_catalog_etag(*result);
    if (request.header("If-None-Match") ==
        std::optional<std::string>(etag)) {
        HttpResponse response;
        response.status = 304;
        response.headers.emplace("ETag", etag);
        response.headers.emplace("Cache-Control", "public, max-age=86400");
        return response;
    }
    Json body = Json::array();
    for (const auto& currency : *result) {
        body.push_back(Json{
            {"code", currency.code},
            {"symbol", currency.symbol},
            {"precision", currency.precision},
            {"displayName", currency.display_name},
            {"isCrypto", currency.is_crypto}});
    }
    auto response = HttpResponseMapper::json(200, body);
    response.headers.emplace("ETag", etag);
    response.headers.emplace("Cache-Control", "public, max-age=86400");
    return response;
}

HttpResponse TimeZoneController::list(const HttpRequest& request) {
    auto result = service_.list_timezones();
    if (!result) return HttpResponseMapper::error(result.error(), request.trace_id);
    const auto etag = timezone_catalog_etag(*result);
    if (request.header("If-None-Match") == std::optional<std::string>(etag)) {
        HttpResponse response;
        response.status = 304;
        response.headers.emplace("ETag", etag);
        response.headers.emplace("Cache-Control", "public, max-age=86400");
        return response;
    }
    Json body = Json::array();
    for (const auto& timezone : *result) {
        body.push_back(Json{
            {"id", timezone.id},
            {"canonicalId", timezone.canonical_id},
            {"isAlias", timezone.is_alias}});
    }
    auto response = HttpResponseMapper::json(200, body);
    response.headers.emplace("ETag", etag);
    response.headers.emplace("Cache-Control", "public, max-age=86400");
    return response;
}

} // namespace pfh::presentation
