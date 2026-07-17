// Personal Finance Hub - Expired Request Idempotency Cleanup

#include "pfh/application/maintenance/cleanup_expired_idempotency_use_case.h"

#include "pfh/application/error_mapping.h"

namespace pfh::application {

Result<std::size_t> CleanupExpiredIdempotencyUseCase::execute() {
    if (batch_size_ == 0 || batch_size_ > 10000) {
        return err(Error::infrastructure_failure(
            "Idempotency cleanup is not configured"));
    }
    auto deleted = repository_.cleanup_expired(clock_.now(), batch_size_);
    return deleted ? Result<std::size_t>(*deleted)
                   : err(from_repository(deleted.error()));
}

} // namespace pfh::application
