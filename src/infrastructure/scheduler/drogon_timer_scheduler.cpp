// Personal Finance Hub - Drogon Event Loop Timer Adapter

#include "pfh/infrastructure/scheduler/drogon_timer_scheduler.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/drogon.h>

#include <chrono>
#include <exception>
#include <utility>

namespace pfh::infrastructure {

domain::RepositoryResult<application::TimerHandle>
DrogonTimerScheduler::schedule_every(
    std::chrono::seconds interval,
    std::function<void()> callback) {
    if (interval <= std::chrono::seconds::zero() || !callback) {
        return std::unexpected(domain::RepositoryError::validation(
            "Recurring timer request is invalid"));
    }
    try {
        auto* loop = drogon::app().getLoop();
        if (loop == nullptr) {
            return std::unexpected(domain::RepositoryError::database(
                "Drogon event loop is unavailable"));
        }
        const auto native_timer = loop->runEvery(
            std::chrono::duration<double>(interval).count(),
            std::move(callback));
        std::scoped_lock lock(mutex_);
        const auto handle = next_handle_++;
        cancellations_.emplace(handle, [loop, native_timer] {
            loop->invalidateTimer(native_timer);
        });
        return handle;
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Failed to register Drogon recurring timer"));
    }
}

void DrogonTimerScheduler::cancel(application::TimerHandle handle) {
    std::function<void()> cancel;
    {
        std::scoped_lock lock(mutex_);
        const auto found = cancellations_.find(handle);
        if (found == cancellations_.end()) {
            return;
        }
        cancel = std::move(found->second);
        cancellations_.erase(found);
    }
    try {
        cancel();
    } catch (...) {
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
