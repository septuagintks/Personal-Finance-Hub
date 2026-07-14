// Personal Finance Hub - Bounded Background Worker Pool

#pragma once

#include "pfh/application/scheduler/i_background_executor.h"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace pfh::infrastructure {

class BoundedThreadPool final : public application::IBackgroundExecutor {
public:
    BoundedThreadPool(std::size_t worker_count, std::size_t queue_capacity);
    ~BoundedThreadPool() override;

    BoundedThreadPool(const BoundedThreadPool&) = delete;
    BoundedThreadPool& operator=(const BoundedThreadPool&) = delete;

    [[nodiscard]] bool submit(std::function<void()> task) override;
    void shutdown() override;

private:
    void worker_loop();

    std::size_t queue_capacity_;
    std::mutex mutex_;
    std::condition_variable wake_workers_;
    std::queue<std::function<void()>> queue_;
    std::vector<std::jthread> workers_;
    bool stopping_ = false;
};

} // namespace pfh::infrastructure
