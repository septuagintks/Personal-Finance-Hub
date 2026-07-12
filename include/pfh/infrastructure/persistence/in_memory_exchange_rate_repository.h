// Personal Finance Hub - In-Memory ExchangeRate Repository
// Version: 1.0
// C++23
//
// Append-only semantics: only INSERT. Historical query uses
// fetched_at <= target_time ORDER BY fetched_at DESC LIMIT 1.

#pragma once

#include "pfh/domain/repositories/i_exchange_rate_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include <map>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

class InMemoryExchangeRateRepository final : public domain::IExchangeRateRepository {
public:
    explicit InMemoryExchangeRateRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<domain::ExchangeRateId> append(
        domain::ITransactionContext& /*tx*/,
        const domain::ExchangeRate& rate) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "append requires an active transaction"));
        }
        if (!rate.rate().is_positive()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Exchange rate must be positive"));
        }
        if (rate.base() == rate.target()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Exchange rate base and target must differ"));
        }
        if (!rate.rate().fits_numeric_20_10()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Exchange rate does not fit NUMERIC(20,10)"));
        }

        const auto id_value = store_.next_exchange_rate_id++;
        // Always insert a new row — never update existing history.
        store_.staged_exchange_rates.emplace(id_value, rate);
        return domain::ExchangeRateId(id_value);
    }

    [[nodiscard]] domain::RepositoryResult<domain::ExchangeRate> find_latest(
        const domain::Currency& base,
        const domain::Currency& target) override {
        const domain::ExchangeRate* best = nullptr;
        for (const auto& [_, rate] : merge_rates()) {
            if (rate.base() != base || rate.target() != target) {
                continue;
            }
            if (best == nullptr || rate.fetched_at() > best->fetched_at()) {
                best = &rate;
            }
        }
        if (best == nullptr) {
            return std::unexpected(domain::RepositoryError::not_found(
                "No exchange rate for " + base.code() + "->" + target.code()));
        }
        return *best;
    }

    [[nodiscard]] domain::RepositoryResult<domain::ExchangeRate> find_historical(
        const domain::Currency& base,
        const domain::Currency& target,
        std::chrono::system_clock::time_point target_time) override {
        const domain::ExchangeRate* best = nullptr;
        for (const auto& [_, rate] : merge_rates()) {
            if (rate.base() != base || rate.target() != target) {
                continue;
            }
            if (rate.fetched_at() > target_time) {
                continue;
            }
            if (best == nullptr || rate.fetched_at() > best->fetched_at()) {
                best = &rate;
            }
        }
        if (best == nullptr) {
            return std::unexpected(domain::RepositoryError::not_found(
                "No historical exchange rate for " + base.code() + "->" +
                target.code() + " at requested time"));
        }
        return *best;
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::ExchangeRate>> find_all_for_pair(
        const domain::Currency& base,
        const domain::Currency& target) override {
        std::vector<domain::ExchangeRate> result;
        for (const auto& [_, rate] : merge_rates()) {
            if (rate.base() == base && rate.target() == target) {
                result.push_back(rate);
            }
        }
        return result;
    }

private:
    [[nodiscard]] std::map<std::int64_t, domain::ExchangeRate> merge_rates() const {
        std::map<std::int64_t, domain::ExchangeRate> merged = store_.exchange_rates;
        if (store_.in_transaction) {
            for (const auto& [id, rate] : store_.staged_exchange_rates) {
                merged.insert_or_assign(id, rate);
            }
        }
        return merged;
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
