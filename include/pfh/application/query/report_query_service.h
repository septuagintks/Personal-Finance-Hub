// Personal Finance Hub - Report Query Service
// Version: 1.0
// C++23
//
// Lightweight CQRS read path for Phase 1 reports.
// - Cash flow explicitly excludes Transfer.
// - Net worth converts account balances to the user's base currency via
//   CurrencyConversionService + ExchangeRate repository.

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/input_constraints.h"
#include "pfh/application/query/i_cash_flow_projection.h"
#include "pfh/domain/currency_conversion_service.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_category_repository.h"
#include "pfh/domain/repositories/i_exchange_rate_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/domain/repositories/i_user_preference_repository.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace pfh::application {

class ReportQueryService {
private:
    using TimePoint = std::chrono::system_clock::time_point;

    class HistoricalRateCache {
    public:
        explicit HistoricalRateCache(domain::IExchangeRateRepository& rates)
            : rates_(rates) {}

        [[nodiscard]] VoidResult prepare_transactions(
            const std::vector<domain::TransactionReadModel>& rows,
            const domain::Currency& report_base) {
            std::vector<std::pair<domain::Currency, TimePoint>> conversions;
            conversions.reserve(rows.size());
            for (const auto& row : rows) {
                if (row.transaction.type() ==
                    domain::TransactionType::Transfer) {
                    continue;
                }
                if (row.transaction.amount().currency() != report_base &&
                    !row.transaction.amount().is_zero()) {
                    conversions.emplace_back(
                        row.transaction.amount().currency(),
                        row.transaction.occurred_at());
                }
            }
            return prepare_conversions(conversions, report_base);
        }

        [[nodiscard]] VoidResult prepare_balances(
            const std::vector<domain::AccountBalanceAt>& balances,
            const domain::Currency& report_base,
            TimePoint at) {
            std::vector<std::pair<domain::Currency, TimePoint>> conversions;
            conversions.reserve(balances.size());
            for (const auto& balance : balances) {
                if (balance.balance.currency() != report_base &&
                    !balance.balance.is_zero()) {
                    conversions.emplace_back(balance.balance.currency(), at);
                }
            }
            return prepare_conversions(conversions, report_base);
        }

        [[nodiscard]] Result<std::optional<domain::ExchangeRate>> find(
            const domain::Currency& base,
            const domain::Currency& target,
            TimePoint at) {
            const PointKey key{base.code(), target.code(), at};
            auto found = point_rates_.find(key);
            if (found == point_rates_.end()) {
                auto loaded = rates_.find_historical_at_points(
                    {domain::HistoricalRatePoint{base, target, at}});
                if (!loaded) return err(from_repository(loaded.error()));
                if (loaded->size() != 1U) {
                    return err(Error::infrastructure_failure(
                        "Historical exchange-rate batch size is inconsistent"));
                }
                found = point_rates_.emplace(key, std::move(loaded->front())).first;
            }
            if (!found->second.has_value()) {
                return std::optional<domain::ExchangeRate>{};
            }
            if (found->second->fetched_at() + std::chrono::hours(24) < at) {
                used_historical_rate_ = true;
            }
            return found->second;
        }

        void reset_evidence() noexcept {
            point_rates_.clear();
            used_historical_rate_ = false;
        }

        [[nodiscard]] ReportRateStatus rate_status() const noexcept {
            return used_historical_rate_
                ? ReportRateStatus::Historical
                : ReportRateStatus::Current;
        }

    private:
        using PointKey = std::tuple<std::string, std::string, TimePoint>;

        [[nodiscard]] VoidResult prepare_conversions(
            const std::vector<std::pair<domain::Currency, TimePoint>>& conversions,
            const domain::Currency& report_base) {
            point_rates_.clear();
            auto usd = domain::Currency::create(domain::Currency::pivot_code());
            if (!usd) return err(from_domain(usd.error()));

            std::map<PointKey, domain::HistoricalRatePoint> unique;
            const auto add = [&](const domain::Currency& base,
                                 const domain::Currency& target,
                                 TimePoint at) {
                unique.try_emplace(
                    PointKey{base.code(), target.code(), at},
                    domain::HistoricalRatePoint{base, target, at});
            };
            for (const auto& [currency, at] : conversions) {
                add(currency, report_base, at);
                add(report_base, currency, at);
                if (currency != *usd && report_base != *usd) {
                    add(*usd, currency, at);
                    add(*usd, report_base, at);
                }
            }
            if (unique.size() > domain::kMaximumHistoricalRatePointBatch) {
                return err(Error::resource_limit(
                    "Historical exchange-rate point limit exceeded"));
            }
            if (unique.empty()) return ok();

            std::vector<domain::HistoricalRatePoint> requested;
            requested.reserve(unique.size());
            for (const auto& [_, point] : unique) requested.push_back(point);
            auto loaded = rates_.find_historical_at_points(requested);
            if (!loaded) return err(from_repository(loaded.error()));
            if (loaded->size() != requested.size()) {
                return err(Error::infrastructure_failure(
                    "Historical exchange-rate batch size is inconsistent"));
            }
            auto value = loaded->begin();
            for (const auto& [key, _] : unique) {
                point_rates_.emplace(key, std::move(*value));
                ++value;
            }
            return ok();
        }

        domain::IExchangeRateRepository& rates_;
        std::map<PointKey, std::optional<domain::ExchangeRate>> point_rates_;
        bool used_historical_rate_ = false;
    };

    struct ConvertedTransaction {
        const domain::Transaction* transaction;
        domain::Money amount;
    };

    struct AccountValue {
        domain::AccountType type;
        domain::Money balance;
    };

    struct ExpenseCategoryAccumulator {
        std::vector<std::optional<domain::CategoryId>> order;
        std::map<std::int64_t, domain::Decimal> amounts;
        std::map<std::int64_t, std::string> names;
        std::map<std::int64_t, std::pair<TimePoint, domain::TransactionId>>
            earliest_transactions;
        domain::Decimal total;
    };

public:
    ReportQueryService(
        domain::IAccountRepository& accounts,
        domain::ITransactionRepository& transactions,
        domain::IExchangeRateRepository& rates,
        domain::IUserPreferenceRepository& preferences,
        domain::ICategoryRepository* categories = nullptr,
        ICashFlowProjection* cash_flow_projection = nullptr)
        : accounts_(accounts),
          transactions_(transactions),
          rates_(rates),
          preferences_(preferences),
          categories_(categories),
          cash_flow_projection_(cash_flow_projection) {}

    // Window is half-open [from, to): `from` inclusive, `to` EXCLUSIVE. This
    // matches the documented month window [month_start, next_month_start) so a
    // transaction stamped exactly at next-month 00:00 lands in the next period,
    // not this one.
    [[nodiscard]] Result<CashFlowDto> cash_flow(
        domain::UserId user_id,
        std::optional<std::chrono::system_clock::time_point> from = std::nullopt,
        std::optional<std::chrono::system_clock::time_point> to = std::nullopt) {
        if (!user_id.is_valid()) {
            return err(Error::validation("User id is invalid"));
        }
        auto pref = preferences_.find_by_user(user_id);
        if (!pref) {
            return err(from_repository(pref.error()));
        }
        auto zero = domain::Decimal::from_integer(0);
        if (!zero) return err(from_domain(zero.error()));
        domain::Money income(*zero, pref->base_currency());
        domain::Money expense(*zero, pref->base_currency());
        HistoricalRateCache rate_cache(rates_);
        TransactionListQuery query;
        query.user_id = user_id;
        query.occurred_from = from;
        query.occurred_to = to;
        auto visited = visit_transaction_pages(
            query,
            kMaximumAggregateReportRows,
            kMaximumReportInputBytes,
            [&](const std::vector<domain::TransactionReadModel>& rows) {
                return rate_cache.prepare_transactions(
                    rows, pref->base_currency());
            },
            [&](const domain::TransactionReadModel& row) -> VoidResult {
                if (row.transaction.type() == domain::TransactionType::Transfer) {
                    return ok();
                }
                auto converted = convert_to_base(
                    row.transaction.amount(), pref->base_currency(),
                    row.transaction.occurred_at(), rate_cache);
                if (!converted) return err(converted.error());
                const ConvertedTransaction value{&row.transaction, std::move(*converted)};
                return add_cash_flow(value, income, expense);
            });
        if (!visited) return err(visited.error());
        return cash_flow_from_totals(income, expense, pref->base_currency());
    }

    [[nodiscard]] Result<NetWorthDto> net_worth(
        domain::UserId user_id,
        std::chrono::system_clock::time_point now =
            std::chrono::system_clock::now()) {
        if (!user_id.is_valid()) {
            return err(Error::validation("User id is invalid"));
        }
        auto pref = preferences_.find_by_user(user_id);
        if (!pref) {
            return err(from_repository(pref.error()));
        }
        auto balances = accounts_.balances_at(user_id, now);
        if (!balances) return err(from_repository(balances.error()));
        HistoricalRateCache rate_cache(rates_);
        auto values = convert_account_balances(
            *balances, pref->base_currency(), now, rate_cache);
        if (!values) {
            return err(values.error());
        }
        return aggregate_net_worth(*values, pref->base_currency(), now);
    }

    // `now` is injectable so tests can pin the reporting window; production
    // callers use the default (system clock). The dashboard's income/expense
    // are scoped to the CURRENT MONTH [month_start, next_month_start), not all
    // history — the standalone cash_flow() keeps its all-history default.
    [[nodiscard]] Result<DashboardSummaryDto> dashboard_summary(
        domain::UserId user_id,
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) {
        if (!user_id.is_valid()) {
            return err(Error::validation("User id is invalid"));
        }
        // Month boundaries are computed in the user's own timezone so a
        // transaction near local midnight on the 1st/last of the month is filed
        // in the correct calendar month, not shifted by the UTC offset.
        auto preference = preferences_.find_by_user(user_id);
        if (!preference) {
            return err(from_repository(preference.error()));
        }
        auto window = current_month_window(now, preference->timezone());
        if (!window) {
            return err(window.error());
        }
        const auto [period_start, period_end] = *window;

        auto balances = accounts_.balances_at(user_id, now);
        if (!balances) return err(from_repository(balances.error()));
        HistoricalRateCache rate_cache(rates_);
        auto account_values = convert_account_balances(
            *balances, preference->base_currency(), now, rate_cache);
        if (!account_values) {
            return err(account_values.error());
        }
        auto net_worth = aggregate_net_worth(
            *account_values, preference->base_currency(), now);
        if (!net_worth) {
            return err(net_worth.error());
        }
        auto zero = domain::Decimal::from_integer(0);
        if (!zero) return err(from_domain(zero.error()));
        domain::Money income(*zero, preference->base_currency());
        domain::Money expense(*zero, preference->base_currency());
        auto categories = load_report_categories(user_id);
        if (!categories) return err(categories.error());
        ExpenseCategoryAccumulator category_totals;
        TransactionListQuery transaction_query;
        transaction_query.user_id = user_id;
        transaction_query.occurred_from = period_start;
        transaction_query.occurred_to = period_end;
        auto visited = visit_transaction_pages(
            transaction_query,
            kMaximumAggregateReportRows,
            kMaximumReportInputBytes,
            [&](const std::vector<domain::TransactionReadModel>& rows) {
                return rate_cache.prepare_transactions(
                    rows, preference->base_currency());
            },
            [&](const domain::TransactionReadModel& row) -> VoidResult {
                if (row.transaction.type() == domain::TransactionType::Transfer) {
                    return ok();
                }
                auto converted = convert_to_base(
                    row.transaction.amount(), preference->base_currency(),
                    row.transaction.occurred_at(), rate_cache);
                if (!converted) return err(converted.error());
                const ConvertedTransaction value{&row.transaction, std::move(*converted)};
                if (auto added = add_cash_flow(value, income, expense); !added) {
                    return added;
                }
                return add_expense_category(
                    value, *categories, category_totals);
            });
        if (!visited) return err(visited.error());
        auto cash_flow = cash_flow_from_totals(
            income, expense, preference->base_currency());
        if (!cash_flow) return err(cash_flow.error());

        DashboardSummaryDto dto;
        dto.currency_code = net_worth->currency_code;
        dto.net_worth = net_worth->total;
        dto.total_assets = net_worth->total_assets;
        dto.total_liabilities = net_worth->total_liabilities;
        dto.income_total = cash_flow->income_total;
        dto.expense_total = cash_flow->expense_total;
        dto.cash_flow_net = cash_flow->net_total;
        dto.account_count = balances->size();
        dto.report_period_start = period_start;
        dto.report_period_end = period_end;
        dto.generated_at = now;

        // Asset distribution: per active account, its base-currency balance and
        // share of total assets (liability accounts are reported with their
        // natural negative amount, matching the REST contract example).
        auto dist = asset_distribution(*account_values);
        if (!dist) {
            return err(dist.error());
        }
        dto.asset_distribution = std::move(*dist);

        // Top expense categories over the current-month window.
        auto cats = finalize_expense_categories(category_totals);
        if (!cats) {
            return err(cats.error());
        }
        dto.top_expense_categories = std::move(*cats);
        return dto;
    }

    /// @brief Inclusive calendar-month trend in the user's configured timezone.
    /// Each bucket is converted to the half-open UTC range [local month start,
    /// next local month start), so DST/UTC offsets cannot move boundary rows.
    [[nodiscard]] Result<CashFlowTrendDto> cash_flow_trend(
        domain::UserId user_id,
        int start_year,
        unsigned start_month,
        int end_year,
        unsigned end_month) {
        namespace ch = std::chrono;
        if (!user_id.is_valid()) {
            return err(Error::validation("User id is invalid"));
        }
        const ch::year_month start{
            ch::year{start_year}, ch::month{start_month}};
        const ch::year_month end{
            ch::year{end_year}, ch::month{end_month}};
        if (!start.ok() || !end.ok() || start > end) {
            return err(Error::validation(
                "Cash-flow month range is invalid"));
        }
        const auto start_serial = static_cast<std::int64_t>(start_year) * 12 +
            static_cast<std::int64_t>(start_month) - 1;
        const auto end_serial = static_cast<std::int64_t>(end_year) * 12 +
            static_cast<std::int64_t>(end_month) - 1;
        if (end_serial - start_serial >=
            static_cast<std::int64_t>(kMaximumReportMonths)) {
            return err(Error::resource_limit(
                "Cash-flow month range cannot exceed 120 months"));
        }

        auto preference = preferences_.find_by_user(user_id);
        if (!preference) {
            return err(from_repository(preference.error()));
        }
        std::vector<std::pair<TimePoint, TimePoint>> windows;
        std::vector<std::string> periods;
        for (auto current = start; current <= end; current += ch::months{1}) {
            auto window = calendar_month_window(current, preference->timezone());
            if (!window) {
                return err(window.error());
            }
            windows.push_back(*window);
            const auto month_number = static_cast<unsigned>(current.month());
            std::string period = std::to_string(static_cast<int>(current.year())) + "-";
            if (month_number < 10) period += "0";
            period += std::to_string(month_number);
            periods.push_back(std::move(period));
        }

        auto zero = domain::Decimal::from_integer(0);
        if (!zero) {
            return err(from_domain(zero.error()));
        }
        std::vector<domain::Money> incomes;
        std::vector<domain::Money> expenses;
        incomes.reserve(windows.size());
        expenses.reserve(windows.size());
        for (std::size_t index = 0; index < windows.size(); ++index) {
            incomes.emplace_back(*zero, preference->base_currency());
            expenses.emplace_back(*zero, preference->base_currency());
        }

        if (cash_flow_projection_ != nullptr) {
            auto projected = cash_flow_projection_->aggregate_monthly(
                CashFlowProjectionQuery{
                    user_id,
                    windows.front().first,
                    windows.back().second,
                    preference->base_currency(),
                    preference->timezone()});
            if (!projected) return err(from_repository(projected.error()));
            if (projected->size() > periods.size()) {
                return err(Error::infrastructure_failure(
                    "Cash-flow projection returned too many month buckets"));
            }
            std::map<std::string, std::size_t> period_indexes;
            for (std::size_t index = 0; index < periods.size(); ++index) {
                period_indexes.emplace(periods[index], index);
            }
            std::set<std::size_t> assigned;
            for (const auto& point : *projected) {
                const auto index = period_indexes.find(point.period);
                if (index == period_indexes.end() ||
                    !assigned.insert(index->second).second) {
                    return err(Error::infrastructure_failure(
                        "Cash-flow projection returned an invalid month bucket"));
                }
                if (point.missing_exchange_rate) {
                    return err(Error(
                        ErrorCode::InvalidExchangeRate,
                        "Missing exchange rate for cash-flow projection",
                        point.period));
                }
                incomes[index->second] = domain::Money(
                    point.income, preference->base_currency());
                expenses[index->second] = domain::Money(
                    point.expense, preference->base_currency());
            }
        } else {
            HistoricalRateCache rate_cache(rates_);
            TransactionListQuery transaction_query;
            transaction_query.user_id = user_id;
            transaction_query.occurred_from = windows.front().first;
            transaction_query.occurred_to = windows.back().second;
            auto visited = visit_transaction_pages(
                transaction_query,
                kMaximumAggregateReportRows,
                kMaximumReportInputBytes,
                [&](const std::vector<domain::TransactionReadModel>& rows) {
                    return rate_cache.prepare_transactions(
                        rows, preference->base_currency());
                },
                [&](const domain::TransactionReadModel& row) -> VoidResult {
                    const auto& transaction = row.transaction;
                    if (transaction.type() == domain::TransactionType::Transfer) {
                        return ok();
                    }
                    const auto occurred_at = transaction.occurred_at();
                    auto after = std::upper_bound(
                        windows.begin(), windows.end(), occurred_at,
                        [](TimePoint value, const auto& window) {
                            return value < window.first;
                        });
                    if (after == windows.begin()) return ok();
                    const auto window = std::prev(after);
                    if (occurred_at < window->first ||
                        occurred_at >= window->second) {
                        return ok();
                    }
                    const auto bucket = static_cast<std::size_t>(
                        std::distance(windows.begin(), window));
                    auto converted = convert_to_base(
                        transaction.amount(), preference->base_currency(),
                        occurred_at, rate_cache);
                    if (!converted) return err(converted.error());
                    const ConvertedTransaction value{
                        &transaction, std::move(*converted)};
                    return add_cash_flow(
                        value, incomes[bucket], expenses[bucket]);
                });
            if (!visited) return err(visited.error());
        }

        CashFlowTrendDto result;
        result.base_currency = preference->base_currency().code();
        result.trends.reserve(windows.size());
        for (std::size_t index = 0; index < windows.size(); ++index) {
            auto flow = cash_flow_from_totals(
                incomes[index], expenses[index], preference->base_currency());
            if (!flow) {
                return err(flow.error());
            }
            result.trends.push_back(CashFlowPeriodDto{
                std::move(periods[index]), flow->income_total,
                flow->expense_total, flow->net_total});
        }
        return result;
    }

    [[nodiscard]] Result<ReportAnalysisDto> analysis(
        const ReportAnalysisQuery& query,
        TimePoint valuation_at = std::chrono::system_clock::now());

    [[nodiscard]] Result<CsvExportDto> export_transactions_csv(
        TransactionListQuery query);

private:
    using CategoryMap = std::map<std::int64_t, domain::Category>;

    [[nodiscard]] static bool valid_transaction_type(
        domain::TransactionType type) noexcept {
        switch (type) {
        case domain::TransactionType::Income:
        case domain::TransactionType::Expense:
        case domain::TransactionType::Transfer:
        case domain::TransactionType::Adjustment:
            return true;
        }
        return false;
    }

    [[nodiscard]] static Result<std::size_t> estimated_transaction_bytes(
        const domain::TransactionReadModel& row) {
        std::size_t total = sizeof(domain::TransactionReadModel);
        const auto add = [&total](std::size_t value) -> bool {
            if (value > std::numeric_limits<std::size_t>::max() - total) {
                return false;
            }
            total += value;
            return true;
        };
        if (!add(row.transaction.description().size()) ||
            (row.category_name.has_value() &&
             !add(row.category_name->size()))) {
            return err(Error::resource_limit(
                "Report input size exceeds the supported budget"));
        }
        for (const auto& tag : row.tags) {
            if (!add(sizeof(domain::Tag)) || !add(tag.name().size())) {
                return err(Error::resource_limit(
                    "Report input size exceeds the supported budget"));
            }
        }
        return total;
    }

    template <typename PagePreparer, typename Visitor>
    [[nodiscard]] VoidResult visit_transaction_pages(
        const TransactionListQuery& query,
        std::size_t maximum_rows,
        std::size_t maximum_bytes,
        PagePreparer&& prepare_page,
        Visitor&& visitor) {
        if (!query.user_id.is_valid() ||
            (query.account_id.has_value() && !query.account_id->is_valid()) ||
            (query.category_id.has_value() && !query.category_id->is_valid()) ||
            (query.tag_id.has_value() && !query.tag_id->is_valid()) ||
            (query.type.has_value() && !valid_transaction_type(*query.type))) {
            return err(Error::validation("Report filters are invalid"));
        }
        if (query.keyword.size() > 128U) {
            return err(Error::validation("keyword exceeds 128 characters"));
        }
        if (query.occurred_from.has_value() &&
            query.occurred_to.has_value() &&
            *query.occurred_from > *query.occurred_to) {
            return err(Error::validation("from cannot be later than to"));
        }

        domain::TransactionPageQuery page_query;
        page_query.user_id = query.user_id;
        page_query.account_id = query.account_id;
        page_query.type = query.type;
        page_query.category_id = query.category_id;
        page_query.tag_id = query.tag_id;
        page_query.occurred_from = query.occurred_from;
        page_query.occurred_to = query.occurred_to;
        page_query.keyword = query.keyword;
        page_query.limit = kReportPageSize;

        std::size_t row_count = 0;
        std::size_t input_bytes = 0;
        while (true) {
            auto page = transactions_.find_page(page_query);
            if (!page) return err(from_repository(page.error()));
            if (page->items.size() > page_query.limit) {
                return err(Error::infrastructure_failure(
                    "Report repository exceeded the requested page size"));
            }
            if (page->has_more && page->items.empty()) {
                return err(Error::infrastructure_failure(
                    "Report pagination did not advance"));
            }
            if (row_count > maximum_rows ||
                page->items.size() > maximum_rows - row_count ||
                (row_count + page->items.size() == maximum_rows &&
                 page->has_more)) {
                return err(Error::resource_limit(
                    "Report row limit exceeded; narrow the requested range"));
            }

            std::size_t page_bytes = 0;
            for (const auto& row : page->items) {
                auto estimated = estimated_transaction_bytes(row);
                if (!estimated) return err(estimated.error());
                if (input_bytes > maximum_bytes ||
                    page_bytes > maximum_bytes - input_bytes ||
                    *estimated > maximum_bytes - input_bytes - page_bytes) {
                    return err(Error::resource_limit(
                        "Report input byte limit exceeded; narrow the requested range"));
                }
                page_bytes += *estimated;
            }

            std::optional<domain::TransactionPageCursor> next_cursor;
            if (page->has_more) {
                const auto& last = page->items.back().transaction;
                next_cursor = domain::TransactionPageCursor{
                    last.occurred_at(), last.id()};
                if (page_query.before.has_value()) {
                    const auto& previous = *page_query.before;
                    const bool advanced =
                        next_cursor->occurred_at < previous.occurred_at ||
                        (next_cursor->occurred_at == previous.occurred_at &&
                         next_cursor->id < previous.id);
                    if (!advanced) {
                        return err(Error::infrastructure_failure(
                            "Report pagination did not advance"));
                    }
                }
            }

            row_count += page->items.size();
            input_bytes += page_bytes;
            if (auto prepared = prepare_page(page->items); !prepared) {
                return prepared;
            }
            for (const auto& row : page->items) {
                if (auto consumed = visitor(row); !consumed) {
                    return consumed;
                }
            }
            if (!page->has_more) break;
            page_query.before = *next_cursor;
        }
        return ok();
    }

    template <typename Visitor>
    [[nodiscard]] VoidResult visit_transaction_pages(
        const TransactionListQuery& query,
        std::size_t maximum_rows,
        std::size_t maximum_bytes,
        Visitor&& visitor) {
        return visit_transaction_pages(
            query,
            maximum_rows,
            maximum_bytes,
            [](const std::vector<domain::TransactionReadModel>&) {
                return ok();
            },
            std::forward<Visitor>(visitor));
    }

    [[nodiscard]] static VoidResult add_cash_flow(
        const ConvertedTransaction& converted,
        domain::Money& income,
        domain::Money& expense) {
        const auto type = converted.transaction->type();
        if (type == domain::TransactionType::Income ||
            (type == domain::TransactionType::Adjustment &&
             !converted.amount.is_negative())) {
            const auto value = converted.amount.is_negative()
                ? converted.amount.negated()
                : converted.amount;
            auto sum = income.add(value);
            if (!sum) {
                return err(from_domain(sum.error()));
            }
            income = *sum;
        } else if (type == domain::TransactionType::Expense ||
                   type == domain::TransactionType::Adjustment) {
            const auto value = converted.amount.is_negative()
                ? converted.amount.negated()
                : converted.amount;
            auto sum = expense.add(value);
            if (!sum) {
                return err(from_domain(sum.error()));
            }
            expense = *sum;
        }
        return ok();
    }

    [[nodiscard]] static Result<CashFlowDto> cash_flow_from_totals(
        const domain::Money& income,
        const domain::Money& expense,
        const domain::Currency& base) {
        auto net = income.subtract(expense);
        if (!net) {
            return err(from_domain(net.error()));
        }
        return CashFlowDto{
            base.code(),
            income.amount().to_string(),
            expense.amount().to_string(),
            net->amount().to_string()};
    }

    [[nodiscard]] Result<std::vector<AccountValue>> convert_account_balances(
        const std::vector<domain::AccountBalanceAt>& balances,
        const domain::Currency& base,
        TimePoint now,
        HistoricalRateCache& rate_cache) {
        if (auto prepared = rate_cache.prepare_balances(balances, base, now);
            !prepared) {
            return err(prepared.error());
        }
        std::vector<AccountValue> result;
        result.reserve(balances.size());
        for (const auto& balance : balances) {
            auto converted = convert_to_base(
                balance.balance, base, now, rate_cache);
            if (!converted) {
                return err(converted.error());
            }
            result.push_back(AccountValue{
                balance.account.type(), std::move(*converted)});
        }
        return result;
    }

    [[nodiscard]] static Result<NetWorthDto> aggregate_net_worth(
        const std::vector<AccountValue>& accounts,
        const domain::Currency& base,
        TimePoint now) {
        auto zero = domain::Decimal::from_integer(0);
        if (!zero) {
            return err(from_domain(zero.error()));
        }
        domain::Money assets(*zero, base);
        domain::Money liabilities(*zero, base);
        for (const auto& account : accounts) {
            domain::Money& bucket = account.balance.amount().is_negative()
                ? liabilities
                : assets;
            auto sum = bucket.add(account.balance);
            if (!sum) {
                return err(from_domain(sum.error()));
            }
            bucket = *sum;
        }
        auto total = assets.add(liabilities);
        if (!total) {
            return err(from_domain(total.error()));
        }
        return NetWorthDto{
            base.code(), total->amount().to_string(),
            assets.amount().to_string(), liabilities.amount().to_string(), now};
    }

    [[nodiscard]] static Result<TimePoint> checked_time_point(
        std::chrono::sys_seconds value) {
        namespace ch = std::chrono;
        const auto minimum = ch::ceil<ch::seconds>(TimePoint::min());
        const auto maximum = ch::floor<ch::seconds>(TimePoint::max());
        if (value < minimum || value > maximum) {
            return err(Error::validation(
                "Reporting month is outside the supported system clock range"));
        }
        return ch::time_point_cast<TimePoint::duration>(value);
    }

    // Compute the half-open UTC window [month_start, next_month_start) for the
    // calendar month that contains `now` AS OBSERVED IN `tz_name`. The month
    // boundary is a local-time concept (local midnight on the 1st), so we:
    //   1. convert `now` to the user's local date,
    //   2. take local midnight of the 1st of this and next month,
    //   3. convert those local instants back to UTC sys_time.
    // The returned bounds are UTC time_points, directly comparable to a
    // transaction's stored (UTC) occurred_at. Unknown zones fail explicitly;
    // silently falling back to UTC would put boundary transactions in the
    // wrong month while making the report look valid.
    [[nodiscard]] static Result<std::pair<TimePoint, TimePoint>> current_month_window(
        TimePoint now, const std::string& tz_name) {
        namespace ch = std::chrono;

        if (tz_name.empty()) {
            return err(Error(
                ErrorCode::ConfigurationError,
                "Configured timezone is unavailable",
                "timezone is empty"));
        }
        const ch::time_zone* zone = nullptr;
        try {
            zone = ch::locate_zone(tz_name);
        } catch (const std::exception&) {
            return err(Error(
                ErrorCode::ConfigurationError,
                "Configured timezone is unavailable",
                tz_name));
        }

        // Local calendar date of `now`.
        const auto local = zone->to_local(now);
        const ch::local_time<ch::days> local_days_point = ch::floor<ch::days>(local);

        const ch::year_month_day ymd{local_days_point};
        const ch::year_month this_month{ymd.year(), ymd.month()};
        const ch::year_month next_month = this_month + ch::months{1};
        const ch::local_days first_local{
            ch::year_month_day{this_month.year(), this_month.month(), ch::day{1}}};
        const ch::local_days next_local{
            ch::year_month_day{next_month.year(), next_month.month(), ch::day{1}}};

        // Convert the two local midnights back to UTC instants.
        const auto start = zone->to_sys(first_local, ch::choose::earliest);
        const auto end = zone->to_sys(next_local, ch::choose::earliest);
        auto checked_start = checked_time_point(start);
        auto checked_end = checked_time_point(end);
        if (!checked_start) return err(checked_start.error());
        if (!checked_end) return err(checked_end.error());
        return std::pair<TimePoint, TimePoint>{*checked_start, *checked_end};
    }

    [[nodiscard]] static Result<std::pair<TimePoint, TimePoint>>
    calendar_month_window(
        std::chrono::year_month month,
        const std::string& tz_name) {
        namespace ch = std::chrono;
        if (tz_name.empty()) {
            return err(Error(
                ErrorCode::ConfigurationError,
                "Configured timezone is unavailable",
                "timezone is empty"));
        }
        const ch::time_zone* zone = nullptr;
        try {
            zone = ch::locate_zone(tz_name);
        } catch (const std::exception&) {
            return err(Error(
                ErrorCode::ConfigurationError,
                "Configured timezone is unavailable",
                tz_name));
        }
        const auto next = month + ch::months{1};
        const ch::local_days start_local{
            ch::year_month_day{month.year(), month.month(), ch::day{1}}};
        const ch::local_days end_local{
            ch::year_month_day{next.year(), next.month(), ch::day{1}}};
        const auto start = zone->to_sys(start_local, ch::choose::earliest);
        const auto end = zone->to_sys(end_local, ch::choose::earliest);
        auto checked_start = checked_time_point(start);
        auto checked_end = checked_time_point(end);
        if (!checked_start) return err(checked_start.error());
        if (!checked_end) return err(checked_end.error());
        return std::pair<TimePoint, TimePoint>{*checked_start, *checked_end};
    }

    // Format `part / whole` as a one-decimal percentage string like "68.0%".
    // whole == 0 yields "0.0%" (avoids divide-by-zero, no misleading share).
    [[nodiscard]] static Result<std::string> format_percentage(
        const domain::Decimal& part, const domain::Decimal& whole) {
        if (whole.is_zero()) {
            return std::string("0.0%");
        }
        auto hundred = domain::Decimal::from_integer(100);
        if (!hundred) {
            return err(from_domain(hundred.error()));
        }
        auto scaled = part.multiply(*hundred);
        if (!scaled) {
            return err(from_domain(scaled.error()));
        }
        auto pct = scaled->divide(whole);
        if (!pct) {
            return err(from_domain(pct.error()));
        }
        auto rounded = pct->round_to_scale(1);
        if (!rounded) {
            return err(from_domain(rounded.error()));
        }
        return format_one_decimal(*rounded) + "%";
    }

    // Render a Decimal with exactly one fractional digit as a plain display
    // string, e.g. 68 -> "68.0", 35.7 -> "35.7". The caller has already
    // applied Half-Even rounding to one fractional digit.
    [[nodiscard]] static std::string format_one_decimal(const domain::Decimal& d) {
        std::string s = d.to_string();
        const auto dot = s.find('.');
        if (dot == std::string::npos) {
            return s + ".0";
        }
        if (dot + 2 > s.size()) {
            s += "0"; // pad to one fractional digit
        }
        return s;
    }

    // Human label for an AccountType (distribution is aggregated by type per
    // 09 §2.1 rule 7, which defaults to AccountType/AccountCategory buckets).
    [[nodiscard]] static std::string account_type_label(domain::AccountType t) {
        switch (t) {
        case domain::AccountType::Cash:          return "Cash";
        case domain::AccountType::Savings:       return "Savings";
        case domain::AccountType::Credit:        return "Credit";
        case domain::AccountType::DigitalWallet: return "DigitalWallet";
        case domain::AccountType::Investment:    return "Investment";
        case domain::AccountType::Crypto:        return "Crypto";
        case domain::AccountType::Other:         return "Other";
        }
        return "Other";
    }

    // Base-currency balance and share of total assets, aggregated by
    // AccountType (not per-account). The percentage denominator is the total of
    // positive (asset) balances, so a net-liability type shows a negative share
    // — matching the REST example where Credit is "-3.7%".
    [[nodiscard]] static Result<std::vector<DistributionSliceDto>>
    asset_distribution(const std::vector<AccountValue>& accounts) {
        auto zero = domain::Decimal::from_integer(0);
        if (!zero) {
            return err(from_domain(zero.error()));
        }

        // Aggregate by AccountType, preserving first-seen order for determinism.
        std::vector<domain::AccountType> order;
        std::map<int, domain::Decimal> by_type;
        domain::Decimal asset_total = *zero;

        for (const auto& account : accounts) {
            const int key = static_cast<int>(account.type);
            if (by_type.find(key) == by_type.end()) {
                by_type.emplace(key, *zero);
                order.push_back(account.type);
            }
            auto sum = by_type.at(key).add(account.balance.amount());
            if (!sum) {
                return err(from_domain(sum.error()));
            }
            by_type.at(key) = *sum;

            // Positive balances contribute to the asset-share denominator.
            if (account.balance.amount().is_positive()) {
                auto asum = asset_total.add(account.balance.amount());
                if (!asum) {
                    return err(from_domain(asum.error()));
                }
                asset_total = *asum;
            }
        }

        std::vector<DistributionSliceDto> result;
        result.reserve(order.size());
        for (const auto& type : order) {
            const auto amount = by_type.at(static_cast<int>(type));
            auto pct = format_percentage(amount, asset_total);
            if (!pct) {
                return err(pct.error());
            }
            DistributionSliceDto dto;
            dto.label = account_type_label(type);
            dto.amount = amount.to_string();
            dto.percentage = *pct;
            result.push_back(std::move(dto));
        }
        return result;
    }

    [[nodiscard]] Result<CategoryMap> load_report_categories(
        domain::UserId user_id) {
        CategoryMap result;
        if (categories_ == nullptr) return result;
        auto categories =
            categories_->find_all_for_user_including_deleted(user_id);
        if (!categories) return err(from_repository(categories.error()));
        if (categories->size() > kMaximumReportMetadataItems) {
            return err(Error::resource_limit(
                "Report category metadata limit exceeded"));
        }
        for (auto& category : *categories) {
            const auto [_, inserted] = result.emplace(
                category.id().value(), std::move(category));
            if (!inserted) {
                return err(Error::infrastructure_failure(
                    "Duplicate category metadata was returned"));
            }
        }
        return result;
    }

    [[nodiscard]] VoidResult add_expense_category(
        const ConvertedTransaction& converted,
        const CategoryMap& category_by_id,
        ExpenseCategoryAccumulator& totals) const {
        const auto& transaction = *converted.transaction;
        const bool outflow =
            transaction.type() == domain::TransactionType::Expense ||
            (transaction.type() == domain::TransactionType::Adjustment &&
             converted.amount.is_negative());
        if (!outflow) return ok();

        const auto magnitude = converted.amount.is_negative()
            ? converted.amount.negated()
            : converted.amount;
        std::optional<domain::CategoryId> bucket = transaction.category_id();
        std::string bucket_name;
        if (categories_ != nullptr && bucket.has_value()) {
            auto current = category_by_id.find(bucket->value());
            if (current == category_by_id.end()) {
                bucket = std::nullopt;
            } else {
                std::set<std::int64_t> visited;
                bool resolved = false;
                for (int depth = 0; depth < domain::kMaxCategoryTreeDepth; ++depth) {
                    const auto& category = current->second;
                    if (!visited.insert(category.id().value()).second) {
                        return err(Error::infrastructure_failure(
                            "Category parent cycle detected"));
                    }
                    if (category.is_root()) {
                        bucket = category.id();
                        bucket_name = category.name();
                        resolved = true;
                        break;
                    }
                    current = category_by_id.find(category.parent_id()->value());
                    if (current == category_by_id.end()) {
                        return err(Error::infrastructure_failure(
                            "Category parent chain is broken"));
                    }
                }
                if (!resolved) {
                    return err(Error::infrastructure_failure(
                        "Category parent chain exceeds the supported depth"));
                }
            }
        }

        const auto key = bucket.has_value() ? bucket->value() : -1;
        auto found = totals.amounts.find(key);
        if (found == totals.amounts.end()) {
            if (totals.order.size() >= kMaximumBreakdownBuckets) {
                return err(Error::resource_limit(
                    "Report category bucket limit exceeded"));
            }
            totals.order.push_back(bucket);
            totals.names.emplace(key, std::move(bucket_name));
            found = totals.amounts.emplace(key, domain::Decimal{}).first;
        }
        const auto occurrence = std::pair{
            transaction.occurred_at(), transaction.id()};
        auto earliest = totals.earliest_transactions.find(key);
        if (earliest == totals.earliest_transactions.end()) {
            totals.earliest_transactions.emplace(key, occurrence);
        } else if (occurrence < earliest->second) {
            earliest->second = occurrence;
        }
        auto sum = found->second.add(magnitude.amount());
        if (!sum) return err(from_domain(sum.error()));
        found->second = *sum;
        auto total = totals.total.add(magnitude.amount());
        if (!total) return err(from_domain(total.error()));
        totals.total = *total;
        return ok();
    }

    [[nodiscard]] static Result<std::vector<CategoryBreakdownDto>>
    finalize_expense_categories(const ExpenseCategoryAccumulator& totals) {
        std::vector<CategoryBreakdownDto> result;
        result.reserve(totals.order.size());
        for (const auto& category : totals.order) {
            const auto key = category.has_value() ? category->value() : -1;
            const auto amount = totals.amounts.find(key);
            const auto name = totals.names.find(key);
            if (amount == totals.amounts.end() || name == totals.names.end()) {
                return err(Error::infrastructure_failure(
                    "Report category aggregation is inconsistent"));
            }
            auto percentage = format_percentage(amount->second, totals.total);
            if (!percentage) return err(percentage.error());
            result.push_back(CategoryBreakdownDto{
                category,
                !name->second.empty()
                    ? name->second
                    : (category.has_value() ? category->to_string() : ""),
                amount->second.to_string(),
                std::move(*percentage)});
        }
        std::stable_sort(
            result.begin(), result.end(),
            [&totals](const auto& left, const auto& right) {
                const auto left_key = left.category_id.has_value()
                    ? left.category_id->value() : -1;
                const auto right_key = right.category_id.has_value()
                    ? right.category_id->value() : -1;
                const auto& left_amount = totals.amounts.at(left_key);
                const auto& right_amount = totals.amounts.at(right_key);
                if (left_amount != right_amount) {
                    return left_amount > right_amount;
                }
                return totals.earliest_transactions.at(left_key) <
                    totals.earliest_transactions.at(right_key);
            });
        return result;
    }

    // Convert `amount` into `base` using rates observed at or before `at`.
    //
    // Fallback chain (all point-in-time, never future rates):
    //   1. same currency               -> identity
    //   2. zero amount                 -> zero in base (no rate required)
    //   3. direct   from -> base        -> multiply
    //   4. reverse  base -> from        -> divide (avoids inverse rounding loss)
    //   5. USD triangulation           -> USD->from & USD->base, cross-rate
    //   6. otherwise                    -> error (missing rate; NOT latest)
    //
    // Reproducibility: the request-local cache batches only the exact business
    // timestamps present on the current bounded page. It never materializes a
    // whole history window and never falls back to a future/latest rate.
    [[nodiscard]] Result<domain::Money> convert_to_base(
        const domain::Money& amount,
        const domain::Currency& base,
        std::chrono::system_clock::time_point at,
        HistoricalRateCache& rate_cache) const {
        if (amount.currency() == base) {
            return amount;
        }
        if (amount.is_zero()) {
            return domain::Money(amount.amount(), base);
        }

        // Look up a rate at-or-before `at`. Only a genuine NotFound may fall
        // through to the next fallback; any other repository error (e.g. a
        // database failure) must abort and propagate as InfrastructureFailure,
        // never be silently downgraded to "missing rate".
        auto lookup = [&rate_cache, at](
                          const domain::Currency& b,
                          const domain::Currency& t)
            -> Result<std::optional<domain::ExchangeRate>> {
            return rate_cache.find(b, t, at);
        };

        // 3. Direct rate: 1 from = rate base.
        auto direct = lookup(amount.currency(), base);
        if (!direct) {
            return err(direct.error());
        }
        if (direct->has_value()) {
            return map_domain(domain::CurrencyConversionService::convert(amount, **direct));
        }

        // 4. Reverse pair: 1 base = rate from. Convert by division to avoid the
        //    rounding loss of inverting the rate first
        //    (e.g. 700 CNY / 7 = 100 USD exactly).
        auto reverse = lookup(base, amount.currency());
        if (!reverse) {
            return err(reverse.error());
        }
        if (reverse->has_value()) {
            auto converted = amount.amount().divide((*reverse)->rate());
            if (!converted) {
                return err(from_domain(converted.error()));
            }
            return domain::Money(*converted, base);
        }

        // 5. USD triangulation for non-USD <-> non-USD pairs.
        auto usd = domain::Currency::create("USD");
        if (!usd) {
            return err(from_domain(usd.error()));
        }
        const bool from_is_usd = (amount.currency() == *usd);
        const bool base_is_usd = (base == *usd);
        if (!from_is_usd && !base_is_usd) {
            auto usd_to_from = lookup(*usd, amount.currency());
            if (!usd_to_from) {
                return err(usd_to_from.error());
            }
            auto usd_to_base = lookup(*usd, base);
            if (!usd_to_base) {
                return err(usd_to_base.error());
            }
            if (usd_to_from->has_value() && usd_to_base->has_value()) {
                // cross_rate(USD->from, USD->base) yields from -> base.
                auto cross = domain::CurrencyConversionService::cross_rate(
                    **usd_to_from, **usd_to_base);
                if (!cross) {
                    return err(from_domain(cross.error()));
                }
                return map_domain(
                    domain::CurrencyConversionService::convert(amount, *cross));
            }
        }

        // 6. No usable point-in-time rate. Do not guess with a future/latest rate.
        return err(Error(ErrorCode::InvalidExchangeRate,
                         "Missing exchange rate for report conversion at the requested time",
                         amount.currency().code() + "->" + base.code()));
    }

    domain::IAccountRepository& accounts_;
    domain::ITransactionRepository& transactions_;
    domain::IExchangeRateRepository& rates_;
    domain::IUserPreferenceRepository& preferences_;
    // Optional: when present, expense breakdown rolls sub-categories up to their
    // first-level parent and resolves human names. When null, falls back to
    // grouping by the raw category id (id string as name).
    domain::ICategoryRepository* categories_ = nullptr;
    ICashFlowProjection* cash_flow_projection_ = nullptr;
};

} // namespace pfh::application
