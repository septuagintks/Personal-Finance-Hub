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
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace pfh::application {

class ReportQueryService {
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
        auto pref = preferences_.find_by_user(user_id);
        if (!pref) {
            return err(from_repository(pref.error()));
        }
        const auto& base = pref->base_currency();

        auto txs = transactions_.find_by_user(user_id, false);
        if (!txs) {
            return err(from_repository(txs.error()));
        }

        auto zero = domain::Decimal::from_integer(0);
        if (!zero) {
            return err(from_domain(zero.error()));
        }
        domain::Money income(*zero, base);
        domain::Money expense(*zero, base);

        for (const auto& tx : *txs) {
            if (tx.type() == domain::TransactionType::Transfer) {
                // Transfer never participates in income/expense stats.
                continue;
            }
            if (from.has_value() && tx.occurred_at() < *from) {
                continue;
            }
            if (to.has_value() && tx.occurred_at() >= *to) { // exclusive upper bound
                continue;
            }

            auto converted = convert_to_base(tx.amount(), base, tx.occurred_at());
            if (!converted) {
                return err(converted.error());
            }

            if (tx.type() == domain::TransactionType::Income) {
                // Income is a magnitude; count it as inflow.
                auto sum = income.add(converted->is_negative()
                                          ? converted->negated()
                                          : *converted);
                if (!sum) {
                    return err(from_domain(sum.error()));
                }
                income = *sum;
            } else if (tx.type() == domain::TransactionType::Expense) {
                // Expense is a magnitude; count it as outflow.
                auto abs_amt = converted->is_negative() ? converted->negated() : *converted;
                auto sum = expense.add(abs_amt);
                if (!sum) {
                    return err(from_domain(sum.error()));
                }
                expense = *sum;
            } else if (tx.type() == domain::TransactionType::Adjustment) {
                // Adjustments are SIGNED: a positive adjustment (refund/subsidy/
                // FX gain) is an inflow and joins income; a negative one (fee/
                // correction/FX loss) is an outflow and joins expense as a
                // positive magnitude. This lets reports represent both, instead
                // of forcing every adjustment into expense.
                if (converted->is_negative()) {
                    auto sum = expense.add(converted->negated());
                    if (!sum) {
                        return err(from_domain(sum.error()));
                    }
                    expense = *sum;
                } else {
                    auto sum = income.add(*converted);
                    if (!sum) {
                        return err(from_domain(sum.error()));
                    }
                    income = *sum;
                }
            }
        }

        auto net = income.subtract(expense);
        if (!net) {
            return err(from_domain(net.error()));
        }

        CashFlowDto dto;
        dto.currency_code = base.code();
        dto.income_total = income.amount().to_string();
        dto.expense_total = expense.amount().to_string();
        dto.net_total = net->amount().to_string();
        return dto;
    }

    [[nodiscard]] Result<NetWorthDto> net_worth(domain::UserId user_id) {
        auto pref = preferences_.find_by_user(user_id);
        if (!pref) {
            return err(from_repository(pref.error()));
        }
        const auto& base = pref->base_currency();

        auto accounts = accounts_.find_active_by_user(user_id);
        if (!accounts) {
            return err(from_repository(accounts.error()));
        }

        auto zero = domain::Decimal::from_integer(0);
        if (!zero) {
            return err(from_domain(zero.error()));
        }
        domain::Money assets(*zero, base);
        domain::Money liabilities(*zero, base);
        const auto now = std::chrono::system_clock::now();

        for (const auto& account : *accounts) {
            auto snapshot = accounts_.balance_of(account.id());
            if (!snapshot) {
                return err(from_repository(snapshot.error()));
            }
            auto converted = convert_to_base(snapshot->balance, base, now);
            if (!converted) {
                return err(converted.error());
            }
            // Split by BALANCE SIGN per the reporting design (09 §2.1): a
            // positive converted balance adds to total_assets, a negative one
            // to total_liabilities. This is independent of account category, so
            // e.g. an overdrawn cash account correctly counts as a liability and
            // a credit account in credit counts as an asset. total = assets +
            // liabilities holds because liabilities stays negative.
            domain::Money& bucket =
                converted->amount().is_negative() ? liabilities : assets;
            auto sum = bucket.add(*converted);
            if (!sum) {
                return err(from_domain(sum.error()));
            }
            bucket = *sum;
        }

        auto total = assets.add(liabilities);
        if (!total) {
            return err(from_domain(total.error()));
        }

        NetWorthDto dto;
        dto.currency_code = base.code();
        dto.total = total->amount().to_string();
        dto.total_assets = assets.amount().to_string();
        dto.total_liabilities = liabilities.amount().to_string();
        dto.generated_at = now;
        return dto;
    }

    // `now` is injectable so tests can pin the reporting window; production
    // callers use the default (system clock). The dashboard's income/expense
    // are scoped to the CURRENT MONTH [month_start, next_month_start), not all
    // history — the standalone cash_flow() keeps its all-history default.
    [[nodiscard]] Result<DashboardSummaryDto> dashboard_summary(
        domain::UserId user_id,
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) {
        // Month boundaries are computed in the user's own timezone so a
        // transaction near local midnight on the 1st/last of the month is filed
        // in the correct calendar month, not shifted by the UTC offset.
        auto pref_for_tz = preferences_.find_by_user(user_id);
        if (!pref_for_tz) {
            return err(from_repository(pref_for_tz.error()));
        }
        auto window = current_month_window(now, pref_for_tz->timezone());
        if (!window) {
            return err(window.error());
        }
        const auto [period_start, period_end] = *window;

        auto nw = net_worth(user_id);
        if (!nw) {
            return err(nw.error());
        }
        auto cf = cash_flow(user_id, period_start, period_end);
        if (!cf) {
            return err(cf.error());
        }
        auto accounts = accounts_.find_active_by_user(user_id);
        if (!accounts) {
            return err(from_repository(accounts.error()));
        }

        DashboardSummaryDto dto;
        dto.currency_code = nw->currency_code;
        dto.net_worth = nw->total;
        dto.total_assets = nw->total_assets;
        dto.total_liabilities = nw->total_liabilities;
        dto.income_total = cf->income_total;
        dto.expense_total = cf->expense_total;
        dto.cash_flow_net = cf->net_total;
        dto.account_count = accounts->size();
        dto.report_period_start = period_start;
        dto.report_period_end = period_end;
        dto.generated_at = now;

        // Asset distribution: per active account, its base-currency balance and
        // share of total assets (liability accounts are reported with their
        // natural negative amount, matching the REST contract example).
        auto dist = asset_distribution(user_id, nw->currency_code, now);
        if (!dist) {
            return err(dist.error());
        }
        dto.asset_distribution = std::move(*dist);

        // Top expense categories over the current-month window.
        auto cats = top_expense_categories(user_id, period_start, period_end);
        if (!cats) {
            return err(cats.error());
        }
        dto.top_expense_categories = std::move(*cats);
        return dto;
    }

private:
    using TimePoint = std::chrono::system_clock::time_point;

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

        const ch::time_zone* zone = nullptr;
        try {
            zone = ch::locate_zone(tz_name.empty() ? "UTC" : tz_name);
        } catch (const std::exception&) {
            return err(Error(
                ErrorCode::ConfigurationError,
                "Configured timezone is unavailable",
                tz_name.empty() ? "UTC" : tz_name));
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
        return std::pair<TimePoint, TimePoint>{
            TimePoint{start.time_since_epoch()},
            TimePoint{end.time_since_epoch()}};
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
        // Round to one decimal place: multiply by 10, truncate to integer via
        // string, then reinsert the decimal point. Decimal has no direct
        // round-to-scale, so format from the canonical string.
        return format_one_decimal(*pct) + "%";
    }

    // Render a Decimal with exactly one fractional digit as a plain display
    // string, e.g. 68 -> "68.0", 35.68 -> "35.6" (display-only truncation of
    // extra digits; the canonical string is already trimmed and half-even
    // rounded at the Decimal scale, so this only affects presentation).
    [[nodiscard]] static std::string format_one_decimal(const domain::Decimal& d) {
        std::string s = d.to_string();
        const auto dot = s.find('.');
        if (dot == std::string::npos) {
            return s + ".0";
        }
        if (dot + 2 < s.size()) {
            s = s.substr(0, dot + 2); // keep exactly one fractional digit
        } else if (dot + 2 > s.size()) {
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
    [[nodiscard]] Result<std::vector<DistributionSliceDto>> asset_distribution(
        domain::UserId user_id,
        const std::string& /*base_code*/,
        TimePoint now) {
        auto pref = preferences_.find_by_user(user_id);
        if (!pref) {
            return err(from_repository(pref.error()));
        }
        const auto& base = pref->base_currency();

        auto accounts = accounts_.find_active_by_user(user_id);
        if (!accounts) {
            return err(from_repository(accounts.error()));
        }

        auto zero = domain::Decimal::from_integer(0);
        if (!zero) {
            return err(from_domain(zero.error()));
        }

        // Aggregate by AccountType, preserving first-seen order for determinism.
        std::vector<domain::AccountType> order;
        std::map<int, domain::Decimal> by_type;
        domain::Decimal asset_total = *zero;

        for (const auto& account : *accounts) {
            auto snapshot = accounts_.balance_of(account.id());
            if (!snapshot) {
                return err(from_repository(snapshot.error()));
            }
            auto converted = convert_to_base(snapshot->balance, base, now);
            if (!converted) {
                return err(converted.error());
            }
            const int key = static_cast<int>(account.type());
            if (by_type.find(key) == by_type.end()) {
                by_type.emplace(key, *zero);
                order.push_back(account.type());
            }
            auto sum = by_type.at(key).add(converted->amount());
            if (!sum) {
                return err(from_domain(sum.error()));
            }
            by_type.at(key) = *sum;

            // Positive balances contribute to the asset-share denominator.
            if (converted->amount().is_positive()) {
                auto asum = asset_total.add(converted->amount());
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

    // Aggregate expense over [from, to) by FIRST-LEVEL (root) category, largest
    // first (09 §2.4 rule 8). When a category repository is available, every
    // transaction's category is rolled up to its top-level parent and the slice
    // carries that root's human name; without one, we fall back to grouping by
    // the raw category id (id string as name). Uncategorized expenses group
    // under an empty category id.
    [[nodiscard]] Result<std::vector<CategoryBreakdownDto>> top_expense_categories(
        domain::UserId user_id,
        TimePoint from,
        TimePoint to) {
        auto pref = preferences_.find_by_user(user_id);
        if (!pref) {
            return err(from_repository(pref.error()));
        }
        const auto& base = pref->base_currency();

        auto txs = transactions_.find_by_user(user_id, false);
        if (!txs) {
            return err(from_repository(txs.error()));
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

        for (const auto& tx : *txs) {
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
            if (tx.occurred_at() < from || tx.occurred_at() >= to) {
                continue;
            }
            auto converted = convert_to_base(tx.amount(), base, tx.occurred_at());
            if (!converted) {
                return err(converted.error());
            }
            // Skip positive (inflow) adjustments; they are not spend.
            if (tx.type() == domain::TransactionType::Adjustment &&
                !converted->is_negative()) {
                continue;
            }
            auto abs_amt =
                converted->is_negative() ? converted->negated() : *converted;

            // Roll the transaction's category up to its first-level parent.
            std::optional<domain::CategoryId> bucket = tx.category_id();
            std::string bucket_name;
            if (categories_ != nullptr && tx.category_id().has_value()) {
                auto root = categories_->resolve_root_id_for_user(
                    *tx.category_id(), user_id);
                if (!root) {
                    // A real repository failure must surface, not be swallowed.
                    if (root.error().status == domain::RepositoryStatus::DatabaseError) {
                        return err(from_repository(root.error()));
                    }
                    // NotFound (e.g. category deleted): treat as uncategorized.
                    bucket = std::nullopt;
                } else {
                    bucket = *root;
                    auto root_cat = categories_->find_by_id_for_user(*root, user_id);
                    if (!root_cat) {
                        return err(from_repository(root_cat.error()));
                    }
                    bucket_name = root_cat->name();
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
        std::sort(result.begin(), result.end(),
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
    //   2. direct   from -> base        -> multiply
    //   3. reverse  base -> from        -> divide (avoids inverse rounding loss)
    //   4. USD triangulation           -> USD->from & USD->base, cross-rate
    //   5. otherwise                    -> error (missing rate; NOT latest)
    //
    // Reproducibility: we only ever call find_historical(at). We deliberately do
    // NOT fall back to find_latest, which could return a rate fetched AFTER `at`
    // and make a historical report non-reproducible. Callers wanting the current
    // value pass `at = now()`, for which find_historical returns the newest rate
    // at-or-before now (i.e. the current rate).
    [[nodiscard]] Result<domain::Money> convert_to_base(
        const domain::Money& amount,
        const domain::Currency& base,
        std::chrono::system_clock::time_point at) const {
        if (amount.currency() == base) {
            return amount;
        }

        // Look up a rate at-or-before `at`. Only a genuine NotFound may fall
        // through to the next fallback; any other repository error (e.g. a
        // database failure) must abort and propagate as InfrastructureFailure,
        // never be silently downgraded to "missing rate".
        auto lookup = [this, at](const domain::Currency& b, const domain::Currency& t)
            -> Result<std::optional<domain::ExchangeRate>> {
            auto r = rates_.find_historical(b, t, at);
            if (r) {
                return std::optional<domain::ExchangeRate>(*r);
            }
            if (r.error().status == domain::RepositoryStatus::NotFound) {
                return std::optional<domain::ExchangeRate>(std::nullopt);
            }
            return err(from_repository(r.error()));
        };

        // 2. Direct rate: 1 from = rate base.
        auto direct = lookup(amount.currency(), base);
        if (!direct) {
            return err(direct.error());
        }
        if (direct->has_value()) {
            return map_domain(domain::CurrencyConversionService::convert(amount, **direct));
        }

        // 3. Reverse pair: 1 base = rate from. Convert by division to avoid the
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

        // 4. USD triangulation for non-USD <-> non-USD pairs.
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

        // 5. No usable point-in-time rate. Do not guess with a future/latest rate.
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
