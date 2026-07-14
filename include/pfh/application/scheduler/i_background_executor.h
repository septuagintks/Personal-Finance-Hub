// Personal Finance Hub - Bounded Background Executor Port

#pragma once

#include <functional>

namespace pfh::application {

class IBackgroundExecutor {
public:
    virtual ~IBackgroundExecutor() = default;

    // Returns false when the executor is stopping or its bounded queue is full.
    [[nodiscard]] virtual bool submit(std::function<void()> task) = 0;
    virtual void shutdown() = 0;
};

} // namespace pfh::application
