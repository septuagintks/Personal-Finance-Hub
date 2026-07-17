// Personal Finance Hub - PostgreSQL Idempotency Cleanup Adapter

#pragma once

#include "pfh/application/maintenance/i_idempotency_cleanup_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class PostgresIdempotencyCleanupRepository final
    : public application::IIdempotencyCleanupRepository {
public:
    explicit PostgresIdempotencyCleanupRepository(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<std::size_t> cleanup_expired(
        std::chrono::system_clock::time_point now,
        std::size_t batch_size) override;

private:
    drogon::orm::DbClientPtr db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
