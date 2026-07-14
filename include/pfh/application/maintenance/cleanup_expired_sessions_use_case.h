// Personal Finance Hub - Cleanup Expired Authentication Data Use Case

#pragma once

#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/maintenance/i_session_cleanup_repository.h"
#include "pfh/application/ports/i_clock.h"

#include <cstddef>

namespace pfh::application {

class CleanupExpiredSessionsUseCase {
public:
    CleanupExpiredSessionsUseCase(
        ISessionCleanupRepository& repository,
        const IClock& clock,
        std::size_t batch_size)
        : repository_(repository), clock_(clock), batch_size_(batch_size) {}

    [[nodiscard]] Result<SessionCleanupSummary> execute() {
        if (batch_size_ == 0) {
            return err(Error(
                ErrorCode::ConfigurationError,
                "Session cleanup batch size must be positive"));
        }
        auto cleaned = repository_.delete_expired(clock_.now(), batch_size_);
        if (!cleaned) {
            return err(from_repository(cleaned.error()));
        }
        return *cleaned;
    }

private:
    ISessionCleanupRepository& repository_;
    const IClock& clock_;
    std::size_t batch_size_;
};

} // namespace pfh::application
