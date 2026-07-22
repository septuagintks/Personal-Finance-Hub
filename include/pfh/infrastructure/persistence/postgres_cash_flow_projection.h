// Personal Finance Hub - PostgreSQL Monthly Cash-flow Projection

#pragma once

#include "pfh/application/query/i_cash_flow_projection.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class PostgresCashFlowProjection final
    : public application::ICashFlowProjection {
public:
    PostgresCashFlowProjection(
        drogon::orm::DbClientPtr db,
        domain::UserId tenant_user_id)
        : db_(std::move(db)), tenant_user_id_(tenant_user_id) {}

    [[nodiscard]] domain::RepositoryResult<
        std::vector<application::CashFlowMonthlyProjection>>
    aggregate_monthly(
        const application::CashFlowProjectionQuery& query) override;

private:
    drogon::orm::DbClientPtr db_;
    domain::UserId tenant_user_id_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
