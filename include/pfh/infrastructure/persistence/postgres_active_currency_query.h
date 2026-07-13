// Personal Finance Hub - PostgreSQL Active Currency Query
// Version: 1.0
// C++23

#pragma once

#include "pfh/application/ports/i_active_currency_query.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>
#include <utility>

namespace pfh::infrastructure {

/// System-scoped scheduler adapter. The supplied client must use a dedicated
/// read-only worker role that can inspect accounts across FORCE RLS policies
/// and read users.base_currency_code.
/// Never pass the request-serving application client to this adapter.
class PostgresActiveCurrencyQuery final
    : public application::IActiveCurrencyQuery {
public:
    explicit PostgresActiveCurrencyQuery(
        drogon::orm::DbClientPtr privileged_read_db)
        : privileged_read_db_(std::move(privileged_read_db)) {}

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Currency>>
    list_active_currencies() override;

private:
    drogon::orm::DbClientPtr privileged_read_db_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
