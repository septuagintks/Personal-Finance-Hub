// Personal Finance Hub - ExchangeRate Repository Interface
// Version: 1.0
// C++23
//
// Exchange rates are append-only. Implementations must never update or delete
// historical rows (DB triggers enforce this in PostgreSQL).

#pragma once

#include "pfh/domain/exchange_rate.h"
#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/typed_id.h"
#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace pfh::domain {

inline constexpr std::size_t kMaximumHistoricalRatePointBatch = 1024;

struct HistoricalRatePoint {
    Currency base;
    Currency target;
    std::chrono::system_clock::time_point at{};
};

class IExchangeRateRepository {
public:
    virtual ~IExchangeRateRepository() = default;

    /// @brief Append a new rate snapshot. Never overwrites history.
    [[nodiscard]] virtual RepositoryResult<ExchangeRateId> append(
        ITransactionContext& tx,
        const ExchangeRate& rate) = 0;

    [[nodiscard]] virtual RepositoryResult<ExchangeRate> find_latest(
        const Currency& base,
        const Currency& target) = 0;

    /// @brief Historical query: latest row with fetched_at <= target_time.
    [[nodiscard]] virtual RepositoryResult<ExchangeRate> find_historical(
        const Currency& base,
        const Currency& target,
        std::chrono::system_clock::time_point target_time) = 0;

    [[nodiscard]] virtual RepositoryResult<std::vector<ExchangeRate>> find_all_for_pair(
        const Currency& base,
        const Currency& target) = 0;

    /// @brief Load the minimum history needed for as-of lookups in [from, to].
    /// Includes the newest row at-or-before `from`, plus rows through `to`.
    [[nodiscard]] virtual RepositoryResult<std::vector<ExchangeRate>>
    find_history_for_pair(
        const Currency& base,
        const Currency& target,
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to) = 0;

    /// @brief Resolve exact as-of rates in request order with one bounded read.
    ///
    /// A missing rate is represented by nullopt. Infrastructure failures abort
    /// the whole batch. Report paths use this instead of materializing every
    /// hourly snapshot in a long time window.
    [[nodiscard]] virtual RepositoryResult<
        std::vector<std::optional<ExchangeRate>>>
    find_historical_at_points(
        const std::vector<HistoricalRatePoint>& points) = 0;
};

} // namespace pfh::domain
