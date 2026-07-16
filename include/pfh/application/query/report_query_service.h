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
#include "pfh/domain/currency_conversion_service.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_category_repository.h"
#include "pfh/domain/repositories/i_exchange_rate_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/domain/repositories/i_user_preference_repository.h"
#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pfh::application {

class ReportQueryService {
private:
    using TimePoint = std::chrono::system_clock::time_point;

    class HistoricalRateCache {
    public:
        HistoricalRateCache(
            domain::IExchangeRateRepository& rates,
            TimePoint from,
            TimePoint to)
            : rates_(rates),
              from_(std::min(from, to)),
              to_(std::max(from, to)) {}

        [[nodiscard]] Result<std::optional<domain::ExchangeRate>> find(
            const domain::Currency& base,
            const domain::Currency& target,
            TimePoint at) {
            const auto key = std::pair{base.code(), target.code()};
            auto found = histories_.find(key);
            if (found == histories_.end()) {
                auto history = rates_.find_history_for_pair(
                    base, target, from_, to_);
                if (!history) {
                    return err(from_repository(history.error()));
                }
                std::stable_sort(
                    history->begin(), history->end(),
                    [](const auto& lhs, const auto& rhs) {
                        return lhs.fetched_at() < rhs.fetched_at();
                    });
                found = histories_.emplace(key, std::move(*history)).first;
            }

            const auto& history = found->second;
            const auto after = std::upper_bound(
                history.begin(), history.end(), at,
                [](TimePoint value, const domain::ExchangeRate& rate) {
                    return value < rate.fetched_at();
                });
            if (after == history.begin()) {
                return std::optional<domain::ExchangeRate>{};
            }
            return std::optional<domain::ExchangeRate>{*std::prev(after)};
        }

    private:
        domain::IExchangeRateRepository& rates_;
        TimePoint from_;
        TimePoint to_;
        std::map<
            std::pair<std::string, std::string>,
            std::vector<domain::ExchangeRate>> histories_;
    };

    struct ConvertedTransaction {
        const domain::Transaction* transaction;
        domain::Money amount;
    };

    struct AccountValue {
        domain::AccountType type;
        domain::Money balance;
    };

public:
    ReportQueryService(
        domain::IAccountRepository& accounts,
        domain::ITransactionRepository& transactions,
        domain::IExchangeRateRepository& rates,
        domain::IUserPreferenceRepository& preferences,
        domain::ICategoryRepository* categories = nullptr)
        : accounts_(accounts),
          transactions_(transactions),
          rates_(rates),
          preferences_(preferences),
          categories_(categories) {}

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
        auto txs = transactions_.find_by_user_in_range(
            user_id, from, to, false);
        if (!txs) {
            return err(from_repository(txs.error()));
        }
        const auto bounds = transaction_time_bounds(*txs);
        HistoricalRateCache rate_cache(rates_, bounds.first, bounds.second);
        auto converted = convert_transactions(
            *txs, pref->base_currency(), rate_cache);
        if (!converted) {
            return err(converted.error());
        }
        return aggregate_cash_flow(*converted, pref->base_currency());
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
        auto accounts = accounts_.find_active_by_user(user_id);
        if (!accounts) {
            return err(from_repository(accounts.error()));
        }

        HistoricalRateCache rate_cache(rates_, now, now);
        auto balances = load_account_values(
            *accounts, pref->base_currency(), now, rate_cache);
        if (!balances) {
            return err(balances.error());
        }
        return aggregate_net_worth(*balances, pref->base_currency(), now);
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

        auto accounts = accounts_.find_active_by_user(user_id);
        if (!accounts) {
            return err(from_repository(accounts.error()));
        }
        auto transactions = transactions_.find_by_user_in_range(
            user_id, period_start, period_end, false);
        if (!transactions) {
            return err(from_repository(transactions.error()));
        }

        const auto bounds = transaction_time_bounds(*transactions);
        const auto rate_from = transactions->empty()
            ? now
            : std::min(now, bounds.first);
        const auto rate_to = transactions->empty()
            ? now
            : std::max(now, bounds.second);
        HistoricalRateCache rate_cache(rates_, rate_from, rate_to);
        auto account_values = load_account_values(
            *accounts, preference->base_currency(), now, rate_cache);
        if (!account_values) {
            return err(account_values.error());
        }
        auto converted_transactions = convert_transactions(
            *transactions, preference->base_currency(), rate_cache);
        if (!converted_transactions) {
            return err(converted_transactions.error());
        }
        auto net_worth = aggregate_net_worth(
            *account_values, preference->base_currency(), now);
        if (!net_worth) {
            return err(net_worth.error());
        }
        auto cash_flow = aggregate_cash_flow(
            *converted_transactions, preference->base_currency());
        if (!cash_flow) {
            return err(cash_flow.error());
        }

        DashboardSummaryDto dto;
        dto.currency_code = net_worth->currency_code;
        dto.net_worth = net_worth->total;
        dto.total_assets = net_worth->total_assets;
        dto.total_liabilities = net_worth->total_liabilities;
        dto.income_total = cash_flow->income_total;
        dto.expense_total = cash_flow->expense_total;
        dto.cash_flow_net = cash_flow->net_total;
        dto.account_count = accounts->size();
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
        auto cats = top_expense_categories(
            user_id, *converted_transactions);
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
        const int start_serial = start_year * 12 +
            static_cast<int>(start_month) - 1;
        const int end_serial = end_year * 12 +
            static_cast<int>(end_month) - 1;
        if (end_serial - start_serial >= 120) {
            return err(Error::validation(
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

        auto transactions = transactions_.find_by_user_in_range(
            user_id, windows.front().first, windows.back().second, false);
        if (!transactions) {
            return err(from_repository(transactions.error()));
        }
        const auto bounds = transaction_time_bounds(*transactions);
        HistoricalRateCache rate_cache(rates_, bounds.first, bounds.second);
        auto converted = convert_transactions(
            *transactions, preference->base_currency(), rate_cache);
        if (!converted) {
            return err(converted.error());
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

        std::size_t bucket = 0;
        for (const auto& transaction : *converted) {
            const auto occurred_at = transaction.transaction->occurred_at();
            while (bucket < windows.size() &&
                   occurred_at >= windows[bucket].second) {
                ++bucket;
            }
            if (bucket >= windows.size()) {
                break;
            }
            if (occurred_at < windows[bucket].first) {
                continue;
            }
            auto added = add_cash_flow(
                transaction, incomes[bucket], expenses[bucket]);
            if (!added) {
                return err(added.error());
            }
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

private:
    [[nodiscard]] static std::pair<TimePoint, TimePoint> transaction_time_bounds(
        const std::vector<domain::Transaction>& transactions) {
        if (transactions.empty()) {
            return {TimePoint{}, TimePoint{}};
        }
        auto earliest = transactions.front().occurred_at();
        auto latest = earliest;
        for (const auto& transaction : transactions) {
            earliest = std::min(earliest, transaction.occurred_at());
            latest = std::max(latest, transaction.occurred_at());
        }
        return {earliest, latest};
    }

    [[nodiscard]] Result<std::vector<ConvertedTransaction>> convert_transactions(
        const std::vector<domain::Transaction>& transactions,
        const domain::Currency& base,
        HistoricalRateCache& rate_cache) const {
        std::vector<ConvertedTransaction> result;
        result.reserve(transactions.size());
        for (const auto& transaction : transactions) {
            if (transaction.type() == domain::TransactionType::Transfer) {
                continue;
            }
            auto converted = convert_to_base(
                transaction.amount(), base, transaction.occurred_at(),
                rate_cache);
            if (!converted) {
                return err(converted.error());
            }
            result.push_back(ConvertedTransaction{
                &transaction, std::move(*converted)});
        }
        return result;
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

    [[nodiscard]] static Result<CashFlowDto> aggregate_cash_flow(
        const std::vector<ConvertedTransaction>& transactions,
        const domain::Currency& base) {
        auto zero = domain::Decimal::from_integer(0);
        if (!zero) {
            return err(from_domain(zero.error()));
        }
        domain::Money income(*zero, base);
        domain::Money expense(*zero, base);
        for (const auto& transaction : transactions) {
            auto added = add_cash_flow(transaction, income, expense);
            if (!added) {
                return err(added.error());
            }
        }
        return cash_flow_from_totals(income, expense, base);
    }

    [[nodiscard]] Result<std::vector<AccountValue>> load_account_values(
        const std::vector<domain::Account>& accounts,
        const domain::Currency& base,
        TimePoint now,
        HistoricalRateCache& rate_cache) {
        std::vector<AccountValue> result;
        result.reserve(accounts.size());
        for (const auto& account : accounts) {
            auto snapshot = accounts_.balance_of(account.id());
            if (!snapshot) {
                return err(from_repository(snapshot.error()));
            }
            auto converted = convert_to_base(
                snapshot->balance, base, now, rate_cache);
            if (!converted) {
                return err(converted.error());
            }
            result.push_back(AccountValue{
                account.type(), std::move(*converted)});
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

    // Aggregate already-converted expense rows by first-level category. The
    // complete historical tree is loaded once so soft-deleted categories remain
    // nameable without a per-transaction recursive repository query.
    [[nodiscard]] Result<std::vector<CategoryBreakdownDto>> top_expense_categories(
        domain::UserId user_id,
        const std::vector<ConvertedTransaction>& transactions) {
        std::map<std::int64_t, domain::Category> category_by_id;
        if (categories_ != nullptr) {
            auto categories =
                categories_->find_all_for_user_including_deleted(user_id);
            if (!categories) {
                return err(from_repository(categories.error()));
            }
            for (auto& category : *categories) {
                category_by_id.emplace(category.id().value(), std::move(category));
            }
        }

        auto zero = domain::Decimal::from_integer(0);
        if (!zero) {
            return err(from_domain(zero.error()));
        }

        // Preserve first-seen order for determinism, keyed by the ROLLED-UP
        // (root) category id value, or -1 for uncategorized.
        std::vector<std::optional<domain::CategoryId>> order;
        std::map<std::int64_t, domain::Decimal> by_cat;
        std::map<std::int64_t, std::string> name_of;
        domain::Decimal expense_total = *zero;

        auto key_of = [](const std::optional<domain::CategoryId>& c) -> std::int64_t {
            return c.has_value() ? c->value() : -1;
        };

        for (const auto& converted : transactions) {
            const auto& tx = *converted.transaction;
            // Expense breakdown counts outflows only: every Expense, and
            // NEGATIVE adjustments (fees/corrections). A positive adjustment is
            // an inflow and does not belong in a spend breakdown. Income and
            // transfers are excluded.
            const bool is_expense = tx.type() == domain::TransactionType::Expense;
            const bool is_outflow_adjustment =
                tx.type() == domain::TransactionType::Adjustment;
            if (!is_expense && !is_outflow_adjustment) {
                continue;
            }
            // Skip positive (inflow) adjustments; they are not spend.
            if (tx.type() == domain::TransactionType::Adjustment &&
                !converted.amount.is_negative()) {
                continue;
            }
            auto abs_amt =
                converted.amount.is_negative()
                    ? converted.amount.negated()
                    : converted.amount;

            // Roll the transaction's category up to its first-level parent.
            std::optional<domain::CategoryId> bucket = tx.category_id();
            std::string bucket_name;
            if (categories_ != nullptr && tx.category_id().has_value()) {
                auto current = category_by_id.find(tx.category_id()->value());
                if (current == category_by_id.end()) {
                    // A physically missing historical category cannot be named,
                    // but the amount still belongs in the report.
                    bucket = std::nullopt;
                } else {
                    std::set<std::int64_t> visited;
                    bool reached_root = false;
                    for (int depth = 0;
                         depth < domain::kMaxCategoryTreeDepth;
                         ++depth) {
                        const auto& node = current->second;
                        if (!visited.insert(node.id().value()).second) {
                            return err(from_repository(
                                domain::RepositoryError::database(
                                    "Category parent cycle detected")));
                        }
                        if (node.is_root()) {
                            bucket = node.id();
                            bucket_name = node.name();
                            reached_root = true;
                            break;
                        }
                        current = category_by_id.find(
                            node.parent_id()->value());
                        if (current == category_by_id.end()) {
                            return err(from_repository(
                                domain::RepositoryError::database(
                                    "Category parent chain is broken")));
                        }
                    }
                    if (!reached_root) {
                        return err(from_repository(
                            domain::RepositoryError::database(
                                "Category parent chain exceeds 64 levels")));
                    }
                }
            }

            const auto k = key_of(bucket);
            if (by_cat.find(k) == by_cat.end()) {
                by_cat.emplace(k, *zero);
                order.push_back(bucket);
                name_of.emplace(k, bucket_name);
            }
            auto sum = by_cat.at(k).add(abs_amt.amount());
            if (!sum) {
                return err(from_domain(sum.error()));
            }
            by_cat.at(k) = *sum;

            auto tsum = expense_total.add(abs_amt.amount());
            if (!tsum) {
                return err(from_domain(tsum.error()));
            }
            expense_total = *tsum;
        }

        std::vector<CategoryBreakdownDto> result;
        result.reserve(order.size());
        for (const auto& cat : order) {
            const auto k = key_of(cat);
            const auto amount = by_cat.at(k);
            auto pct = format_percentage(amount, expense_total);
            if (!pct) {
                return err(pct.error());
            }
            CategoryBreakdownDto dto;
            dto.category_id = cat;
            // Prefer the resolved root name; fall back to the id string.
            const auto& resolved = name_of.at(k);
            dto.category_name =
                !resolved.empty() ? resolved
                                  : (cat.has_value() ? cat->to_string() : "");
            dto.amount = amount.to_string();
            dto.percentage = *pct;
            result.push_back(std::move(dto));
        }
        // Largest amount first. Compare by parsed Decimal to avoid string order.
        std::stable_sort(
            result.begin(), result.end(),
            [](const CategoryBreakdownDto& a, const CategoryBreakdownDto& b) {
                auto da = domain::Decimal::parse(a.amount);
                auto db = domain::Decimal::parse(b.amount);
                if (!da || !db) {
                    return false;
                }
                return *da > *db;
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
    // Reproducibility: the request-local cache contains an anchor at-or-before
    // the report window plus all snapshots inside it. It never falls back to a
    // future/latest rate.
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
};

} // namespace pfh::application
