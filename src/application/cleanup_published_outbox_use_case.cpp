// Personal Finance Hub - Published Outbox Retention

#include "pfh/application/maintenance/cleanup_published_outbox_use_case.h"

#include "pfh/application/error_mapping.h"

namespace pfh::application {

Result<std::size_t> CleanupPublishedOutboxUseCase::execute() {
    if (retention_ < std::chrono::hours(24) ||
        retention_ > std::chrono::hours(24 * 3650) ||
        batch_size_ == 0 || batch_size_ > 10000) {
        return err(Error::infrastructure_failure(
            "Outbox retention is not configured"));
    }
    auto deleted = repository_.cleanup_published(
        clock_.now(), retention_, batch_size_);
    return deleted ? Result<std::size_t>(*deleted)
                   : err(from_repository(deleted.error()));
}

} // namespace pfh::application
