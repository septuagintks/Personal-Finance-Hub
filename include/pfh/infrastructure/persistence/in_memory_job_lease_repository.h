// Personal Finance Hub - In-Memory Scheduled Job Lease Repository

#pragma once

#include "pfh/application/scheduler/i_job_lease_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <mutex>
#include <string>

namespace pfh::infrastructure {

class InMemoryJobLeaseRepository final
    : public application::IJobLeaseRepository {
public:
    explicit InMemoryJobLeaseRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<std::optional<application::JobLease>>
    try_acquire(
        std::string_view job_name,
        std::string_view owner_id,
        std::chrono::system_clock::time_point now,
        std::chrono::seconds lease_duration) override {
        if (!valid_identity(job_name) || !valid_identity(owner_id) ||
            lease_duration <= std::chrono::seconds::zero()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Scheduled job lease request is invalid"));
        }
        std::scoped_lock lock(store_.mutex);
        const auto found = store_.scheduled_job_leases.find(
            std::string(job_name));
        if (found != store_.scheduled_job_leases.end() &&
            found->second.lease_until > now) {
            return std::optional<application::JobLease>{};
        }

        const std::string token = "job-lease-" +
                                  std::to_string(
                                      store_.next_job_lease_token++);
        const auto lease_until = now + lease_duration;
        store_.scheduled_job_leases.insert_or_assign(
            std::string(job_name),
            InMemoryJobLeaseRecord{
                std::string(owner_id), token, lease_until, now});
        return std::optional<application::JobLease>(
            application::JobLease{token, lease_until});
    }

    [[nodiscard]] domain::RepositoryResult<bool> release(
        std::string_view job_name,
        std::string_view owner_id,
        std::string_view lease_token,
        std::chrono::system_clock::time_point released_at) override {
        if (!valid_identity(job_name) || !valid_identity(owner_id) ||
            lease_token.empty()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Scheduled job lease release is invalid"));
        }
        std::scoped_lock lock(store_.mutex);
        const auto found = store_.scheduled_job_leases.find(
            std::string(job_name));
        if (found == store_.scheduled_job_leases.end() ||
            found->second.owner_id != owner_id ||
            found->second.lease_token != lease_token) {
            return false;
        }
        found->second.lease_until = released_at;
        found->second.updated_at = released_at;
        return true;
    }

private:
    [[nodiscard]] static bool valid_identity(std::string_view value) {
        return !value.empty() && value.size() <= 128;
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
