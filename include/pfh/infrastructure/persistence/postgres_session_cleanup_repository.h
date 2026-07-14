// Personal Finance Hub - PostgreSQL Authentication Data Cleanup

#pragma once

#include "pfh/application/maintenance/i_session_cleanup_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class PostgresSessionCleanupRepository final
    : public application::ISessionCleanupRepository {
public:
    explicit PostgresSessionCleanupRepository(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<application::SessionCleanupSummary>
    delete_expired(
        std::chrono::system_clock::time_point now,
        std::size_t batch_size) override;

private:
    drogon::orm::DbClientPtr db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
