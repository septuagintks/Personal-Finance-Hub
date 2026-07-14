// Personal Finance Hub - RefreshExchangeRatesUseCase
// Version: 1.0
// C++23
//
// Pulls external rates (provider port), appends historical snapshots, and
// degrades gracefully when provider fails (no throw; returns degraded result).

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/application/ports/i_active_currency_query.h"
#include "pfh/application/ports/i_clock.h"
#include "pfh/application/ports/i_exchange_rate_provider.h"
#include "pfh/domain/events/domain_events.h"
#include "pfh/domain/repositories/i_exchange_rate_repository.h"
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace pfh::application {

class RefreshExchangeRatesUseCase {
public:
    RefreshExchangeRatesUseCase(
        IActiveCurrencyQuery& active_currencies,
        domain::IExchangeRateRepository& rates,
        IExchangeRateProvider& provider,
        IUnitOfWork& uow,
        const IClock& clock)
        : active_currencies_(active_currencies),
          rates_(rates),
          provider_(provider),
          uow_(uow),
          clock_(clock) {}

    [[nodiscard]] Result<RefreshExchangeRatesResultDto> execute(
        const RefreshExchangeRatesCommand& cmd = {}) {
        auto pivot = domain::Currency::create("USD");
        if (!pivot) {
            return err(from_domain(pivot.error()));
        }

        std::vector<domain::Currency> targets;
        std::set<std::string> target_codes;
        if (!cmd.target_currency_codes.empty()) {
            for (const auto& code : cmd.target_currency_codes) {
                auto c = domain::Currency::create(code);
                if (!c) {
                    return err(from_domain(c.error()));
                }
                if (!(*c == *pivot) && target_codes.insert(c->code()).second) {
                    targets.push_back(*c);
                }
            }
        } else {
            auto active = active_currencies_.list_active_currencies();
            if (!active) {
                return err(from_repository(active.error()));
            }
            for (const auto& c : *active) {
                if (!(c == *pivot) && target_codes.insert(c.code()).second) {
                    targets.push_back(c);
                }
            }
        }

        if (targets.empty()) {
            RefreshExchangeRatesResultDto dto;
            dto.appended_count = 0;
            dto.degraded = false;
            dto.message = "No target currencies to refresh";
            return dto;
        }

        auto fetched = provider_.fetch_latest(*pivot, targets);
        if (!fetched) {
            // Degrade: do not fail hard; leave historical rates in place. But
            // record a degradation/alert event and verify a fallback actually
            // exists, so "degraded with no usable historical rate" is
            // distinguishable from a soft, recoverable outage.
            bool historical_fallback_available = true;
            for (const auto& target : targets) {
                auto latest = rates_.find_latest(*pivot, target);
                if (latest) {
                    continue;
                }
                // A NotFound just means no rate for this pair; any other status
                // is a real repository error and should surface.
                if (latest.error().status != domain::RepositoryStatus::NotFound) {
                    return err(from_repository(latest.error()));
                }
                historical_fallback_available = false;
            }

            const std::string reason =
                fetched.error().status == domain::RepositoryStatus::DatabaseError
                    ? "provider unavailable"
                    : "provider error";
            auto alert = uow_.execute_in_transaction(
                [&](domain::ITransactionContext& /*tx*/) -> domain::RepositoryVoidResult {
                    uow_.register_event(
                        std::make_shared<domain::ExchangeRateRefreshFailedEvent>(
                            std::string(provider_.provider_name()),
                            pivot->code(),
                            historical_fallback_available,
                            reason,
                            clock_.now()));
                    return {};
                });
            if (!alert) {
                return err(from_repository(alert.error()));
            }

            RefreshExchangeRatesResultDto dto;
            dto.appended_count = 0;
            dto.degraded = true;
            dto.message = historical_fallback_available
                              ? "Provider unavailable; using historical rates"
                              : "Provider unavailable; historical fallback is incomplete";
            return dto;
        }

        std::set<std::string> returned_targets;
        for (const auto& rate : *fetched) {
            if (!(rate.base() == *pivot) ||
                !target_codes.contains(rate.target().code())) {
                return err(Error(
                    ErrorCode::ExternalServiceError,
                    "Exchange-rate provider returned an unexpected currency pair"));
            }
            if (!returned_targets.insert(rate.target().code()).second) {
                return err(Error(
                    ErrorCode::ExternalServiceError,
                    "Exchange-rate provider returned a duplicate currency pair"));
            }
        }
        if (returned_targets != target_codes) {
            return err(Error(
                ErrorCode::ExternalServiceError,
                "Exchange-rate provider response is missing requested currencies"));
        }

        std::size_t appended = 0;
        auto write = uow_.execute_in_transaction(
            [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
                for (const auto& rate : *fetched) {
                    auto id = rates_.append(tx, rate);
                    if (!id) {
                        return std::unexpected(id.error());
                    }
                    ++appended;
                    uow_.register_event(
                        std::make_shared<domain::ExchangeRateRefreshedEvent>(
                            rate.source(),
                            rate.base().code(),
                            rate.target().code(),
                            rate.fetched_at()));
                }
                return {};
            });
        if (!write) {
            return err(from_repository(write.error()));
        }

        RefreshExchangeRatesResultDto dto;
        dto.appended_count = appended;
        dto.degraded = false;
        dto.message = "Refreshed " + std::to_string(appended) + " rates";
        return dto;
    }

private:
    IActiveCurrencyQuery& active_currencies_;
    domain::IExchangeRateRepository& rates_;
    IExchangeRateProvider& provider_;
    IUnitOfWork& uow_;
    const IClock& clock_;
};

} // namespace pfh::application
