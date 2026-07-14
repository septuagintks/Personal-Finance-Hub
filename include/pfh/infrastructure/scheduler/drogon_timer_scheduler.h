// Personal Finance Hub - Drogon Event Loop Timer Adapter

#pragma once

#include "pfh/application/scheduler/i_timer_scheduler.h"

#ifdef PFH_HAS_POSTGRESQL

#include <functional>
#include <map>
#include <mutex>

namespace pfh::infrastructure {

class DrogonTimerScheduler final : public application::ITimerScheduler {
public:
    [[nodiscard]] domain::RepositoryResult<application::TimerHandle>
    schedule_every(
        std::chrono::seconds interval,
        std::function<void()> callback) override;

    void cancel(application::TimerHandle handle) override;

private:
    std::mutex mutex_;
    std::map<application::TimerHandle, std::function<void()>> cancellations_;
    application::TimerHandle next_handle_ = 1;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
