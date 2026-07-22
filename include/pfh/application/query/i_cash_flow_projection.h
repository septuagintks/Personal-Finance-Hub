// Personal Finance Hub - Bounded Cash-flow Read Projection

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/decimal.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/typed_id.h"

#include <chrono>
#include <string>
#include <vector>

namespace pfh::application {

struct CashFlowProjectionQuery {
    domain::UserId user_id;
    std::chrono::system_clock::time_point from{};
    std::chrono::system_clock::time_point to{};
    domain::Currency base_currency;
    std::string timezone;
};

struct CashFlowMonthlyProjection {
    std::string period;
    domain::Decimal income;
    domain::Decimal expense;
    bool missing_exchange_rate = false;
};

class ICashFlowProjection {
public:
    virtual ~ICashFlowProjection() = default;

    /// Returns only observed months. Callers fill absent months with zero.
    /// Implementations must enforce the shared aggregate row/input-byte limits
    /// before conversion and return at most 120 sorted, unique month buckets.
    [[nodiscard]] virtual domain::RepositoryResult<
        std::vector<CashFlowMonthlyProjection>>
    aggregate_monthly(const CashFlowProjectionQuery& query) = 0;
};

} // namespace pfh::application
