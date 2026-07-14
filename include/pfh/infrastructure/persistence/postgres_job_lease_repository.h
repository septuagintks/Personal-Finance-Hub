// Personal Finance Hub - PostgreSQL Scheduled Job Lease Repository

#pragma once

#include "pfh/application/scheduler/i_job_lease_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class PostgresJobLeaseRepository final
    : public application::IJobLeaseRepository {
public:
    explicit PostgresJobLeaseRepository(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<std::optional<application::JobLease>>
    try_acquire(
        std::string_view job_name,
        std::string_view owner_id,
        std::chrono::system_clock::time_point now,
        std::chrono::seconds lease_duration) override;

    [[nodiscard]] domain::RepositoryResult<bool> release(
        std::string_view job_name,
        std::string_view owner_id,
        std::string_view lease_token,
        std::chrono::system_clock::time_point released_at) override;

private:
    drogon::orm::DbClientPtr db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
