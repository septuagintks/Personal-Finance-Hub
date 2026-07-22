// Personal Finance Hub - Bounded Background Worker Pool

#pragma once

#include "pfh/application/scheduler/i_background_executor.h"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

class BoundedThreadPool final : public application::IBackgroundExecutor {
public:
    BoundedThreadPool(
        std::size_t worker_count,
        std::size_t queue_capacity,
        std::size_t weight_capacity = 0);
    ~BoundedThreadPool() override;

    BoundedThreadPool(const BoundedThreadPool&) = delete;
    BoundedThreadPool& operator=(const BoundedThreadPool&) = delete;

    [[nodiscard]] bool submit(std::function<void()> task) override;
    [[nodiscard]] bool submit_weighted(
        std::function<void()> task,
        std::size_t weight) override;
    void shutdown() override;

    [[nodiscard]] std::size_t reserved_weight() const;

private:
    struct WeightedTask {
        std::function<void()> action;
        std::size_t weight = 0;
    };

    void worker_loop();

    std::size_t queue_capacity_;
    std::size_t weight_capacity_;
    mutable std::mutex mutex_;
    std::condition_variable wake_workers_;
    std::queue<WeightedTask> queue_;
    std::vector<std::jthread> workers_;
    std::size_t reserved_weight_ = 0;
    bool stopping_ = false;
};

} // namespace pfh::infrastructure
