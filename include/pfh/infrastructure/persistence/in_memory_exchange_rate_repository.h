// Personal Finance Hub - In-Memory ExchangeRate Repository
// Version: 1.0
// C++23
//
// Append-only semantics: only INSERT. Historical query uses
// fetched_at <= target_time ORDER BY fetched_at DESC, id DESC LIMIT 1.

#pragma once

#include "pfh/domain/repositories/i_exchange_rate_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include <algorithm>
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
        std::int64_t best_id = 0;
        const auto merged = merge_rates();
        for (const auto& [id, rate] : merged) {
            if (rate.base() != base || rate.target() != target) {
                continue;
            }
            if (best == nullptr || rate.fetched_at() > best->fetched_at() ||
                (rate.fetched_at() == best->fetched_at() && id > best_id)) {
                best = &rate;
                best_id = id;
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
        std::int64_t best_id = 0;
        const auto merged = merge_rates();
        for (const auto& [id, rate] : merged) {
            if (rate.base() != base || rate.target() != target) {
                continue;
            }
            if (rate.fetched_at() > target_time) {
                continue;
            }
            if (best == nullptr || rate.fetched_at() > best->fetched_at() ||
                (rate.fetched_at() == best->fetched_at() && id > best_id)) {
                best = &rate;
                best_id = id;
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
        std::vector<std::pair<std::int64_t, domain::ExchangeRate>> matching;
        for (const auto& [id, rate] : merge_rates()) {
            if (rate.base() == base && rate.target() == target) {
                matching.emplace_back(id, rate);
            }
        }
        std::sort(matching.begin(), matching.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.second.fetched_at() != rhs.second.fetched_at()) {
                return lhs.second.fetched_at() < rhs.second.fetched_at();
            }
            return lhs.first < rhs.first;
        });

        std::vector<domain::ExchangeRate> result;
        result.reserve(matching.size());
        for (auto& [_, rate] : matching) {
            result.push_back(std::move(rate));
        }
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::ExchangeRate>>
    find_history_for_pair(
        const domain::Currency& base,
        const domain::Currency& target,
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to) override {
        if (to < from) {
            return std::unexpected(domain::RepositoryError::validation(
                "Exchange-rate history range is invalid"));
        }
        std::map<
            std::chrono::system_clock::time_point,
            std::pair<std::int64_t, domain::ExchangeRate>> latest_by_time;
        const domain::ExchangeRate* anchor = nullptr;
        std::int64_t anchor_id = 0;
        const auto merged = merge_rates();
        for (const auto& [id, rate] : merged) {
            if (rate.base() != base || rate.target() != target ||
                rate.fetched_at() > to) {
                continue;
            }
            if (rate.fetched_at() <= from) {
                if (anchor == nullptr || rate.fetched_at() > anchor->fetched_at() ||
                    (rate.fetched_at() == anchor->fetched_at() && id > anchor_id)) {
                    anchor = &rate;
                    anchor_id = id;
                }
                continue;
            }
            auto [position, inserted] = latest_by_time.try_emplace(
                rate.fetched_at(), id, rate);
            if (!inserted && id > position->second.first) {
                position->second = {id, rate};
            }
        }

        std::vector<domain::ExchangeRate> result;
        result.reserve(latest_by_time.size() + (anchor == nullptr ? 0U : 1U));
        if (anchor != nullptr) {
            result.push_back(*anchor);
        }
        for (auto& [_, entry] : latest_by_time) {
            result.push_back(std::move(entry.second));
        }
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<
        std::vector<std::optional<domain::ExchangeRate>>>
    find_historical_at_points(
        const std::vector<domain::HistoricalRatePoint>& points) override {
        if (points.size() > domain::kMaximumHistoricalRatePointBatch) {
            return std::unexpected(domain::RepositoryError::resource_limit(
                "Historical rate point batch exceeds 1024 items"));
        }

        std::map<
            std::pair<std::string, std::string>,
            std::vector<std::pair<std::int64_t, domain::ExchangeRate>>> rates_by_pair;
        for (const auto& [id, rate] : merge_rates()) {
            rates_by_pair[{rate.base().code(), rate.target().code()}]
                .emplace_back(id, rate);
        }
        for (auto& [_, rates] : rates_by_pair) {
            std::sort(rates.begin(), rates.end(), [](const auto& lhs, const auto& rhs) {
                if (lhs.second.fetched_at() != rhs.second.fetched_at()) {
                    return lhs.second.fetched_at() < rhs.second.fetched_at();
                }
                return lhs.first < rhs.first;
            });
        }

        std::vector<std::optional<domain::ExchangeRate>> result;
        result.reserve(points.size());
        for (const auto& point : points) {
            const auto pair = rates_by_pair.find(
                {point.base.code(), point.target.code()});
            if (pair == rates_by_pair.end()) {
                result.emplace_back(std::nullopt);
                continue;
            }
            const auto after = std::upper_bound(
                pair->second.begin(), pair->second.end(), point.at,
                [](const auto value, const auto& entry) {
                    return value < entry.second.fetched_at();
                });
            result.emplace_back(
                after == pair->second.begin()
                    ? std::optional<domain::ExchangeRate>{}
                    : std::optional<domain::ExchangeRate>{std::prev(after)->second});
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
