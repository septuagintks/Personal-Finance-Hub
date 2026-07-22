// Personal Finance Hub - Phase 2 report analysis and CSV export

#include "pfh/application/query/report_query_service.h"

#include "pfh/application/transaction_dto_mapper.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pfh::application {

namespace {

constexpr int kMaximumReportMonths = 120;
constexpr auto kMaximumExportRange = std::chrono::days(366);

struct BreakdownTotals {
    std::string key;
    std::string label;
    domain::Decimal income;
    domain::Decimal expense;
};

[[nodiscard]] std::string transaction_type_text(domain::TransactionType type) {
    switch (type) {
    case domain::TransactionType::Income: return "income";
    case domain::TransactionType::Expense: return "expense";
    case domain::TransactionType::Transfer: return "transfer";
    case domain::TransactionType::Adjustment: return "adjustment";
    }
    return "adjustment";
}

[[nodiscard]] std::string business_amount(const TransactionDto& transaction) {
    if (transaction.type == domain::TransactionType::Expense &&
        !transaction.amount.empty() && transaction.amount.front() == '-') {
        return transaction.amount.substr(1);
    }
    return transaction.amount;
}

[[nodiscard]] std::string protect_csv_text(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first != std::string::npos &&
        (value[first] == '=' || value[first] == '+' ||
         value[first] == '-' || value[first] == '@')) {
        value.insert(value.begin(), '\'');
    }
    return value;
}

[[nodiscard]] bool append_csv_cell(
    std::string& output,
    std::string_view value,
    bool prepend_comma) {
    const auto quotes = static_cast<std::size_t>(
        std::count(value.begin(), value.end(), '"'));
    const std::size_t punctuation = prepend_comma ? 3U : 2U;
    if (value.size() > kMaximumCsvOutputBytes ||
        quotes > kMaximumCsvOutputBytes - value.size() ||
        punctuation > kMaximumCsvOutputBytes - value.size() - quotes) {
        return false;
    }
    const auto required = value.size() + quotes + punctuation;
    if (output.size() > kMaximumCsvOutputBytes ||
        required > kMaximumCsvOutputBytes - output.size()) {
        return false;
    }
    if (prepend_comma) output.push_back(',');
    output.push_back('"');
    for (const char character : value) {
        if (character == '"') output.push_back('"');
        output.push_back(character);
    }
    output.push_back('"');
    return true;
}

[[nodiscard]] std::string local_rfc3339(
    std::chrono::system_clock::time_point value,
    const std::chrono::time_zone& zone) {
    namespace ch = std::chrono;
    const auto seconds = ch::floor<ch::seconds>(value);
    const auto info = zone.get_info(seconds);
    const auto local = seconds + info.offset;
    const auto days = ch::floor<ch::days>(local);
    const ch::year_month_day date(days);
    const ch::hh_mm_ss time(local - days);
    const auto offset_seconds = info.offset.count();
    const auto offset_abs = offset_seconds < 0
        ? -offset_seconds : offset_seconds;
    const auto offset_hours = offset_abs / 3600;
    const auto offset_minutes = (offset_abs % 3600) / 60;

    std::ostringstream output;
    output << std::setfill('0')
           << std::setw(4) << static_cast<int>(date.year()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.month()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.day()) << 'T'
           << std::setw(2) << time.hours().count() << ':'
           << std::setw(2) << time.minutes().count() << ':'
           << std::setw(2) << time.seconds().count()
           << (offset_seconds < 0 ? '-' : '+')
           << std::setw(2) << offset_hours << ':'
           << std::setw(2) << offset_minutes;
    return output.str();
}

[[nodiscard]] std::string local_date(
    std::chrono::system_clock::time_point value,
    const std::chrono::time_zone& zone) {
    namespace ch = std::chrono;
    const auto seconds = ch::floor<ch::seconds>(value);
    const auto local = seconds + zone.get_info(seconds).offset;
    const ch::year_month_day date(ch::floor<ch::days>(local));
    std::ostringstream output;
    output << std::setfill('0')
           << std::setw(4) << static_cast<int>(date.year())
           << std::setw(2) << static_cast<unsigned>(date.month())
           << std::setw(2) << static_cast<unsigned>(date.day());
    return output.str();
}

} // namespace

Result<ReportAnalysisDto> ReportQueryService::analysis(
    const ReportAnalysisQuery& query,
    TimePoint valuation_at) {
    namespace ch = std::chrono;
    if (!query.user_id.is_valid()) {
        return err(Error::validation("User id is invalid"));
    }
    const ch::year_month start{
        ch::year{query.start_year}, ch::month{query.start_month}};
    const ch::year_month end{
        ch::year{query.end_year}, ch::month{query.end_month}};
    if (!start.ok() || !end.ok() || start > end) {
        return err(Error::validation("Report month range is invalid"));
    }
    const auto start_serial = query.start_year * 12 +
        static_cast<int>(query.start_month) - 1;
    const auto end_serial = query.end_year * 12 +
        static_cast<int>(query.end_month) - 1;
    if (end_serial - start_serial >= kMaximumReportMonths) {
        return err(Error::validation(
            "Report month range cannot exceed 120 months"));
    }

    auto preference = preferences_.find_by_user(query.user_id);
    if (!preference) return err(from_repository(preference.error()));

    std::vector<std::pair<TimePoint, TimePoint>> windows;
    std::vector<std::string> periods;
    for (auto month = start; month <= end; month += ch::months{1}) {
        auto window = calendar_month_window(month, preference->timezone());
        if (!window) return err(window.error());
        windows.push_back(*window);
        const auto number = static_cast<unsigned>(month.month());
        periods.push_back(
            std::to_string(static_cast<int>(month.year())) + "-" +
            (number < 10 ? "0" : "") + std::to_string(number));
    }
    if (windows.front().first > valuation_at) {
        return err(Error::validation("Report range cannot start in the future"));
    }
    if (windows.back().first > valuation_at) {
        return err(Error::validation("Report range cannot end in the future"));
    }

    TransactionListQuery row_query;
    row_query.user_id = query.user_id;
    row_query.occurred_from = windows.front().first;
    row_query.occurred_to = std::min(windows.back().second, valuation_at);

    auto all_accounts = accounts_.find_by_user(query.user_id, std::nullopt);
    if (!all_accounts) return err(from_repository(all_accounts.error()));
    if (all_accounts->size() > kMaximumReportMetadataItems) {
        return err(Error::validation(
            "Report account metadata limit exceeded"));
    }
    std::map<std::int64_t, domain::Account> account_by_id;
    for (auto& account : *all_accounts) {
        const auto [_, inserted] = account_by_id.emplace(
            account.id().value(), std::move(account));
        if (!inserted) {
            return err(Error::infrastructure_failure(
                "Duplicate account metadata was returned"));
        }
    }

    auto categories = load_report_categories(query.user_id);
    if (!categories) return err(categories.error());

    HistoricalRateCache rates(rates_);
    ReportAnalysisDto result;
    result.base_currency = preference->base_currency().code();
    result.valuation_at = valuation_at;
    result.report_period_start = windows.front().first;
    result.report_period_end =
        std::min(windows.back().second, valuation_at);
    result.dimension = query.dimension;
    result.dimension_overlaps = query.dimension == ReportDimension::Tag;

    bool any_historical = false;
    result.net_worth_trend.reserve(windows.size());
    std::vector<TimePoint> valuation_points;
    valuation_points.reserve(windows.size());
    for (const auto& window : windows) {
        const auto period_end = std::min(window.second, valuation_at);
        valuation_points.push_back(period_end == valuation_at
            ? valuation_at
            : period_end - ch::microseconds(1));
    }
    auto balance_projections = accounts_.balances_at_many(
        query.user_id, valuation_points);
    if (!balance_projections) {
        return err(from_repository(balance_projections.error()));
    }
    if (balance_projections->size() != windows.size()) {
        return err(Error::infrastructure_failure(
            "Historical balance projection count is inconsistent"));
    }
    for (std::size_t index = 0; index < windows.size(); ++index) {
        const auto valued_at = valuation_points[index];
        rates.reset_evidence();
        std::vector<AccountValue> values;
        values.reserve((*balance_projections)[index].balances.size());
        for (const auto& balance : (*balance_projections)[index].balances) {
            auto converted = convert_to_base(
                balance.balance, preference->base_currency(), valued_at, rates);
            if (!converted) return err(converted.error());
            values.push_back(AccountValue{
                balance.account.type(), std::move(*converted)});
        }
        auto net_worth = aggregate_net_worth(
            values, preference->base_currency(), valued_at);
        if (!net_worth) return err(net_worth.error());
        auto point_status = rates.rate_status();
        if (valued_at + ch::hours(24) < valuation_at) {
            point_status = ReportRateStatus::Historical;
        }
        any_historical = any_historical ||
            point_status == ReportRateStatus::Historical;
        result.net_worth_trend.push_back(NetWorthTrendPointDto{
            periods[index],
            net_worth->total_assets,
            net_worth->total_liabilities,
            net_worth->total,
            valued_at,
            point_status});
    }

    auto zero = domain::Decimal::from_integer(0);
    if (!zero) return err(from_domain(zero.error()));
    std::map<std::string, BreakdownTotals> totals;
    std::size_t breakdown_expansions = 0;
    const auto add_bucket = [&](
        const std::string& key,
        const std::string& label,
        const domain::Decimal& amount,
        bool income) -> VoidResult {
        auto position = totals.find(key);
        bool inserted = false;
        if (position == totals.end()) {
            if (totals.size() >= kMaximumBreakdownBuckets) {
                return err(Error::validation(
                    "Report breakdown bucket limit exceeded"));
            }
            position = totals.emplace(
                key, BreakdownTotals{key, label, *zero, *zero}).first;
            inserted = true;
        }
        if (!inserted && position->second.label.empty()) {
            position->second.label = label;
        }
        auto sum = income
            ? position->second.income.add(amount)
            : position->second.expense.add(amount);
        if (!sum) return err(from_domain(sum.error()));
        if (income) position->second.income = *sum;
        else position->second.expense = *sum;
        return ok();
    };

    rates.reset_evidence();
    auto visited = visit_transaction_pages(
        row_query,
        kMaximumDetailedReportRows,
        kMaximumReportInputBytes,
        [&](const domain::TransactionReadModel& row) -> VoidResult {
        const auto& transaction = row.transaction;
        if (transaction.type() == domain::TransactionType::Transfer) return ok();
        auto converted = convert_to_base(
            transaction.amount(), preference->base_currency(),
            transaction.occurred_at(), rates);
        if (!converted) return err(converted.error());
        const bool income = transaction.type() == domain::TransactionType::Income ||
            (transaction.type() == domain::TransactionType::Adjustment &&
             !converted->is_negative());
        const bool expense = transaction.type() == domain::TransactionType::Expense ||
            (transaction.type() == domain::TransactionType::Adjustment &&
             converted->is_negative());
        if (!income && !expense) return ok();
        const auto magnitude = converted->is_negative()
            ? converted->amount().negated() : converted->amount();

        if (query.dimension == ReportDimension::Account) {
            const auto account = account_by_id.find(
                transaction.account_id().value());
            ++breakdown_expansions;
            return add_bucket(
                "account:" + transaction.account_id().to_string(),
                account == account_by_id.end()
                    ? transaction.account_id().to_string()
                    : account->second.name(),
                magnitude, income);
        }
        if (query.dimension == ReportDimension::Tag) {
            const auto expansion_count = std::max<std::size_t>(1U, row.tags.size());
            if (breakdown_expansions > kMaximumBreakdownExpansions ||
                expansion_count >
                    kMaximumBreakdownExpansions - breakdown_expansions) {
                return err(Error::validation(
                    "Report tag expansion limit exceeded"));
            }
            breakdown_expansions += expansion_count;
            if (row.tags.empty()) {
                return add_bucket(
                    "tag:untagged", "Untagged", magnitude, income);
            }
            for (const auto& tag : row.tags) {
                if (auto added = add_bucket(
                        "tag:" + tag.id().to_string(), tag.name(),
                        magnitude, income);
                    !added) {
                    return added;
                }
            }
            return ok();
        }

        ++breakdown_expansions;
        std::optional<domain::CategoryId> root = transaction.category_id();
        std::string label = "Uncategorized";
        if (root.has_value() && categories_ != nullptr) {
            auto current = categories->find(root->value());
            if (current == categories->end()) {
                root = std::nullopt;
            } else {
                std::set<std::int64_t> parent_ids;
                bool resolved = false;
                for (int depth = 0; depth < domain::kMaxCategoryTreeDepth; ++depth) {
                    const auto& category = current->second;
                    if (!parent_ids.insert(category.id().value()).second) {
                        return err(Error::infrastructure_failure(
                            "Category parent cycle detected"));
                    }
                    if (category.is_root()) {
                        root = category.id();
                        label = category.name();
                        resolved = true;
                        break;
                    }
                    current = categories->find(category.parent_id()->value());
                    if (current == categories->end()) {
                        return err(Error::infrastructure_failure(
                            "Category parent chain is broken"));
                    }
                }
                if (!resolved) {
                    return err(Error::infrastructure_failure(
                        "Category parent chain exceeds the supported depth"));
                }
            }
        } else if (root.has_value()) {
            label = root->to_string();
        }
        return add_bucket(
            root.has_value() ? "category:" + root->to_string()
                             : "category:uncategorized",
            label, magnitude, income);
    });
    if (!visited) return err(visited.error());
    any_historical = any_historical ||
        rates.rate_status() == ReportRateStatus::Historical ||
        windows.front().second + ch::hours(24) < valuation_at;

    result.breakdown.reserve(totals.size());
    for (const auto& [_, total] : totals) {
        auto net = total.income.subtract(total.expense);
        if (!net) return err(from_domain(net.error()));
        result.breakdown.push_back(ReportBreakdownSliceDto{
            total.key,
            total.label,
            total.income.to_string(),
            total.expense.to_string(),
            net->to_string()});
    }
    std::stable_sort(
        result.breakdown.begin(), result.breakdown.end(),
        [](const auto& left, const auto& right) {
            auto left_expense = domain::Decimal::parse(left.expense);
            auto right_expense = domain::Decimal::parse(right.expense);
            if (left_expense && right_expense &&
                *left_expense != *right_expense) {
                return *left_expense > *right_expense;
            }
            auto left_income = domain::Decimal::parse(left.income);
            auto right_income = domain::Decimal::parse(right.income);
            if (left_income && right_income && *left_income != *right_income) {
                return *left_income > *right_income;
            }
            return left.label < right.label;
        });
    result.rate_status = any_historical
        ? ReportRateStatus::Historical
        : ReportRateStatus::Current;
    return result;
}

Result<CsvExportDto> ReportQueryService::export_transactions_csv(
    TransactionListQuery query) {
    if (!query.occurred_from.has_value() || !query.occurred_to.has_value()) {
        return err(Error::validation("from and to are required"));
    }
    if (*query.occurred_from > *query.occurred_to) {
        return err(Error::validation("from cannot be later than to"));
    }
    if (*query.occurred_to - *query.occurred_from > kMaximumExportRange) {
        return err(Error::validation(
            "Report transaction range cannot exceed 366 days"));
    }
    auto preference = preferences_.find_by_user(query.user_id);
    if (!preference) return err(from_repository(preference.error()));

    const std::chrono::time_zone* zone = nullptr;
    try {
        zone = std::chrono::locate_zone(preference->timezone());
    } catch (const std::exception&) {
        return err(Error(
            ErrorCode::ConfigurationError,
            "Configured timezone is unavailable",
            preference->timezone()));
    }

    std::string csv = "\xEF\xBB\xBF";
    csv += "id,occurred_at,type,account_id,amount,currency_code,category,tags,description,transfer_group_id\r\n";
    csv.reserve(64U * 1024U);
    std::size_t row_count = 0;
    auto visited = visit_transaction_pages(
        query,
        kMaximumDetailedReportRows,
        kMaximumReportInputBytes,
        [&](const domain::TransactionReadModel& row) -> VoidResult {
        const auto transaction = to_transaction_dto(row);
        std::string tags;
        for (std::size_t index = 0; index < transaction.tags.size(); ++index) {
            if (index != 0) tags += '|';
            tags += transaction.tags[index].name;
        }
        const std::string group = transaction.transfer_group_id.has_value()
            ? transaction.transfer_group_id->to_string() : "";
        const std::vector<std::string> cells{
            transaction.id.to_string(),
            local_rfc3339(transaction.occurred_at, *zone),
            transaction_type_text(transaction.type),
            transaction.account_id.to_string(),
            business_amount(transaction),
            transaction.currency_code,
            protect_csv_text(transaction.category_name.value_or("")),
            protect_csv_text(std::move(tags)),
            protect_csv_text(transaction.description),
            group};
        for (std::size_t index = 0; index < cells.size(); ++index) {
            if (!append_csv_cell(csv, cells[index], index != 0)) {
                return err(Error::validation(
                    "CSV output byte limit exceeded; narrow the requested range"));
            }
        }
        if (csv.size() > kMaximumCsvOutputBytes - 2U) {
            return err(Error::validation(
                "CSV output byte limit exceeded; narrow the requested range"));
        }
        csv += "\r\n";
        ++row_count;
        return ok();
    });
    if (!visited) return err(visited.error());

    const auto export_end = *query.occurred_to > *query.occurred_from
        ? *query.occurred_to - std::chrono::seconds(1)
        : *query.occurred_to;
    return CsvExportDto{
        "transactions-" + local_date(*query.occurred_from, *zone) + "-" +
            local_date(export_end, *zone) + ".csv",
        std::move(csv),
        row_count};
}

} // namespace pfh::application
