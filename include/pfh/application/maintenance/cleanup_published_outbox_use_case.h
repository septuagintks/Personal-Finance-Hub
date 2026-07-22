// Personal Finance Hub - Published Outbox Retention

#pragma once

#include "pfh/application/error.h"
#include "pfh/application/maintenance/i_outbox_retention_repository.h"
#include "pfh/application/ports/i_clock.h"

#include <chrono>
#include <cstddef>

namespace pfh::application {

class CleanupPublishedOutboxUseCase {
public:
    CleanupPublishedOutboxUseCase(
        IOutboxRetentionRepository& repository,
        const IClock& clock,
        std::chrono::seconds retention,
        std::size_t batch_size)
        : repository_(repository),
          clock_(clock),
          retention_(retention),
          batch_size_(batch_size) {}

    [[nodiscard]] Result<std::size_t> execute();

private:
    IOutboxRetentionRepository& repository_;
    const IClock& clock_;
    std::chrono::seconds retention_{};
    std::size_t batch_size_ = 0;
};

} // namespace pfh::application
