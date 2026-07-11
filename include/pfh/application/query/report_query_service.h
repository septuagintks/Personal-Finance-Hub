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
#include "pfh/domain/repositories/i_exchange_rate_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/domain/repositories/i_user_preference_repository.h"
#include <chrono>
#include <optional>

namespace pfh::application {

class ReportQueryService {
public:
    ReportQueryService(
        domain::IAccountRepository& accounts,
        domain::ITransactionRepository& transactions,
        domain::IExchangeRateRepository& rates,
        domain::IUserPreferenceRepository& preferences)
        : accounts_(accounts),
          transactions_(transactions),
          rates_(rates),
          preferences_(preferences) {}

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
            if (to.has_value() && tx.occurred_at() > *to) {
                continue;
            }

            auto converted = convert_to_base(tx.amount(), base, tx.occurred_at());
            if (!converted) {
                return err(converted.error());
            }

            if (tx.type() == domain::TransactionType::Income) {
                auto sum = income.add(converted->is_negative()
                                          ? converted->negated()
                                          : *converted);
                if (!sum) {
                    return err(from_domain(sum.error()));
                }
                income = *sum;
            } else if (tx.type() == domain::TransactionType::Expense ||
                       tx.type() == domain::TransactionType::Adjustment) {
                // Expense/fee adjustments contribute to expense total as positive outflow.
                auto abs_amt = converted->is_negative() ? converted->negated() : *converted;
                auto sum = expense.add(abs_amt);
                if (!sum) {
                    return err(from_domain(sum.error()));
                }
                expense = *sum;
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
        domain::Money total(*zero, base);

        for (const auto& account : *accounts) {
            auto snapshot = accounts_.balance_of(account.id());
            if (!snapshot) {
                return err(from_repository(snapshot.error()));
            }
            auto converted = convert_to_base(
                snapshot->balance, base, std::chrono::system_clock::now());
            if (!converted) {
                return err(converted.error());
            }
            auto sum = total.add(*converted);
            if (!sum) {
                return err(from_domain(sum.error()));
            }
            total = *sum;
        }

        NetWorthDto dto;
        dto.currency_code = base.code();
        dto.total = total.amount().to_string();
        return dto;
    }

    [[nodiscard]] Result<DashboardSummaryDto> dashboard_summary(domain::UserId user_id) {
        auto nw = net_worth(user_id);
        if (!nw) {
            return err(nw.error());
        }
        auto cf = cash_flow(user_id);
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
        dto.income_total = cf->income_total;
        dto.expense_total = cf->expense_total;
        dto.cash_flow_net = cf->net_total;
        dto.account_count = accounts->size();
        return dto;
    }

private:
    [[nodiscard]] Result<domain::Money> convert_to_base(
        const domain::Money& amount,
        const domain::Currency& base,
        std::chrono::system_clock::time_point at) const {
        if (amount.currency() == base) {
            return amount;
        }

        // Prefer direct rate: 1 amount.currency = rate base
        auto direct = rates_.find_historical(amount.currency(), base, at);
        if (!direct) {
            direct = rates_.find_latest(amount.currency(), base);
        }
        if (direct) {
            return map_domain(domain::CurrencyConversionService::convert(amount, *direct));
        }

        // Reverse pair: 1 base = rate amount.currency
        // Convert by division to avoid inverse-rate rounding loss
        // (e.g. 700 CNY / 7 USD/CNY = 100 USD exactly).
        auto reverse = rates_.find_historical(base, amount.currency(), at);
        if (!reverse) {
            reverse = rates_.find_latest(base, amount.currency());
        }
        if (!reverse) {
            return err(Error(ErrorCode::InvalidExchangeRate,
                             "Missing exchange rate for report conversion",
                             amount.currency().code() + "->" + base.code()));
        }

        auto converted_amount = amount.amount().divide(reverse->rate());
        if (!converted_amount) {
            return err(from_domain(converted_amount.error()));
        }
        return domain::Money(*converted_amount, base);
    }

    domain::IAccountRepository& accounts_;
    domain::ITransactionRepository& transactions_;
    domain::IExchangeRateRepository& rates_;
    domain::IUserPreferenceRepository& preferences_;
};

} // namespace pfh::application
