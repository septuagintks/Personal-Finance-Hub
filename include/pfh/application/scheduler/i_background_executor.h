// Personal Finance Hub - Bounded Background Executor Port

#pragma once

#include <cstddef>
#include <functional>
#include <utility>

namespace pfh::application {

class IBackgroundExecutor {
public:
    virtual ~IBackgroundExecutor() = default;

    // Returns false when the executor is stopping or its bounded queue is full.
    [[nodiscard]] virtual bool submit(std::function<void()> task) = 0;

    // Weighted submission lets adapters bound retained payload bytes in
    // addition to task count. Executors without a weight budget preserve the
    // original behavior.
    [[nodiscard]] virtual bool submit_weighted(
        std::function<void()> task,
        std::size_t weight) {
        (void)weight;
        return submit(std::move(task));
    }

    virtual void shutdown() = 0;
};

} // namespace pfh::application
