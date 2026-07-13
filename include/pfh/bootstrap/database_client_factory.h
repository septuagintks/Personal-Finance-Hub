// Personal Finance Hub - Production Database Client Factory

#pragma once

#include "pfh/application/error.h"
#include "pfh/infrastructure/config.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

namespace pfh::bootstrap {

class DatabaseClientFactory {
public:
    [[nodiscard]] static application::Result<drogon::orm::DbClientPtr> create(
        const infrastructure::DatabaseConfig& config);

    [[nodiscard]] static application::VoidResult verify_request_role(
        const drogon::orm::DbClientPtr& client,
        std::string_view expected_role);

    [[nodiscard]] static application::VoidResult verify_background_role(
        const drogon::orm::DbClientPtr& client,
        std::string_view expected_role);
};

} // namespace pfh::bootstrap

#endif // PFH_HAS_POSTGRESQL
