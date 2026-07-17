// Personal Finance Hub - Cross-Tenant Idempotency Cleanup Port

#pragma once

#include "pfh/domain/repositories/repository_error.h"

#include <chrono>
#include <cstddef>

namespace pfh::application {

class IIdempotencyCleanupRepository {
public:
    virtual ~IIdempotencyCleanupRepository() = default;

    [[nodiscard]] virtual domain::RepositoryResult<std::size_t> cleanup_expired(
        std::chrono::system_clock::time_point now,
        std::size_t batch_size) = 0;
};

} // namespace pfh::application
