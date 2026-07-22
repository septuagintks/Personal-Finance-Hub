// Personal Finance Hub - Published Outbox Retention Port

#pragma once

#include "pfh/domain/repositories/repository_error.h"

#include <chrono>
#include <cstddef>

namespace pfh::application {

class IOutboxRetentionRepository {
public:
    virtual ~IOutboxRetentionRepository() = default;

    [[nodiscard]] virtual domain::RepositoryResult<std::size_t>
    cleanup_published(
        std::chrono::system_clock::time_point now,
        std::chrono::seconds retention,
        std::size_t batch_size) = 0;
};

} // namespace pfh::application
