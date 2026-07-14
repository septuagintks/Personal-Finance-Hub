// Personal Finance Hub - Expired Authentication Data Cleanup Port

#pragma once

#include "pfh/domain/repositories/repository_error.h"

#include <chrono>
#include <cstddef>

namespace pfh::application {

struct SessionCleanupSummary {
    std::size_t refresh_tokens_deleted = 0;
    std::size_t revoked_access_tokens_deleted = 0;
    std::size_t revoked_sessions_deleted = 0;

    [[nodiscard]] std::size_t total_deleted() const noexcept {
        return refresh_tokens_deleted + revoked_access_tokens_deleted +
               revoked_sessions_deleted;
    }
};

class ISessionCleanupRepository {
public:
    virtual ~ISessionCleanupRepository() = default;

    [[nodiscard]] virtual domain::RepositoryResult<SessionCleanupSummary>
    delete_expired(
        std::chrono::system_clock::time_point now,
        std::size_t batch_size) = 0;
};

} // namespace pfh::application
