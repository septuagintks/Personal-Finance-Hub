// Personal Finance Hub - Recurring Timer Port

#pragma once

#include "pfh/domain/repositories/repository_error.h"

#include <chrono>
#include <cstdint>
#include <functional>

namespace pfh::application {

using TimerHandle = std::uint64_t;

class ITimerScheduler {
public:
    virtual ~ITimerScheduler() = default;

    [[nodiscard]] virtual domain::RepositoryResult<TimerHandle> schedule_every(
        std::chrono::seconds interval,
        std::function<void()> callback) = 0;
    virtual void cancel(TimerHandle handle) = 0;
};

} // namespace pfh::application
