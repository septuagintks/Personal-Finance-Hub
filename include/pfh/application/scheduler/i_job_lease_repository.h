// Personal Finance Hub - Distributed Scheduled Job Lease Port

#pragma once

#include "pfh/domain/repositories/repository_error.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace pfh::application {

struct JobLease {
    std::string token;
    std::chrono::system_clock::time_point lease_until{};
};

class IJobLeaseRepository {
public:
    virtual ~IJobLeaseRepository() = default;

    [[nodiscard]] virtual domain::RepositoryResult<std::optional<JobLease>>
    try_acquire(
        std::string_view job_name,
        std::string_view owner_id,
        std::chrono::system_clock::time_point now,
        std::chrono::seconds lease_duration) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<bool> release(
        std::string_view job_name,
        std::string_view owner_id,
        std::string_view lease_token,
        std::chrono::system_clock::time_point released_at) = 0;
};

} // namespace pfh::application
