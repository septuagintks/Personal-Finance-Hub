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
#include <vector>

namespace pfh::domain {

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
};

} // namespace pfh::domain
