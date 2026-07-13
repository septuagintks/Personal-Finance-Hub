// Personal Finance Hub - PostgreSQL ExchangeRate Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_exchange_rate_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for IExchangeRateRepository.
///
/// `exchange_rates` is NOT RLS-scoped (V1 §RLS). Repository runs without GUC
/// binding. The `forbid_exchange_rate_mutation()` trigger installed in V1
/// guarantees we cannot UPDATE/DELETE accidentally.
class ExchangeRateRepositoryImpl final : public domain::IExchangeRateRepository {
public:
    explicit ExchangeRateRepositoryImpl(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<domain::ExchangeRateId> append(
        domain::ITransactionContext& tx,
        const domain::ExchangeRate& rate) override;

    [[nodiscard]] domain::RepositoryResult<domain::ExchangeRate> find_latest(
        const domain::Currency& base,
        const domain::Currency& target) override;

    [[nodiscard]] domain::RepositoryResult<domain::ExchangeRate> find_historical(
        const domain::Currency& base,
        const domain::Currency& target,
        std::chrono::system_clock::time_point target_time) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::ExchangeRate>> find_all_for_pair(
        const domain::Currency& base,
        const domain::Currency& target) override;

private:
    drogon::orm::DbClientPtr db_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL