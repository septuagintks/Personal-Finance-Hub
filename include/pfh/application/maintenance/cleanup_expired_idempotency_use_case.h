// Personal Finance Hub - Expired Request Idempotency Cleanup

#pragma once

#include "pfh/application/error.h"
#include "pfh/application/maintenance/i_idempotency_cleanup_repository.h"
#include "pfh/application/ports/i_clock.h"

#include <cstddef>

namespace pfh::application {

class CleanupExpiredIdempotencyUseCase {
public:
    CleanupExpiredIdempotencyUseCase(
        IIdempotencyCleanupRepository& repository,
        const IClock& clock,
        std::size_t batch_size)
        : repository_(repository), clock_(clock), batch_size_(batch_size) {}

    [[nodiscard]] Result<std::size_t> execute();

private:
    IIdempotencyCleanupRepository& repository_;
    const IClock& clock_;
    std::size_t batch_size_ = 0;
};

} // namespace pfh::application
