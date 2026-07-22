// Personal Finance Hub - Bounded Background Worker Pool

#include "pfh/infrastructure/scheduler/bounded_thread_pool.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace pfh::infrastructure {

BoundedThreadPool::BoundedThreadPool(
    std::size_t worker_count,
    std::size_t queue_capacity,
    std::size_t weight_capacity)
    : queue_capacity_(queue_capacity),
      weight_capacity_(weight_capacity) {
    if (worker_count == 0 || queue_capacity == 0) {
        throw std::invalid_argument(
            "Background worker count and queue capacity must be positive");
    }
    workers_.reserve(worker_count);
    try {
        for (std::size_t index = 0; index < worker_count; ++index) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        wake_workers_.notify_all();
        workers_.clear();
        throw;
    }
}

BoundedThreadPool::~BoundedThreadPool() {
    shutdown();
}

bool BoundedThreadPool::submit(std::function<void()> task) {
    return submit_weighted(std::move(task), 0);
}

bool BoundedThreadPool::submit_weighted(
    std::function<void()> task,
    std::size_t weight) {
    if (!task) {
        return false;
    }
    {
        std::scoped_lock lock(mutex_);
        const bool weight_exceeded = weight_capacity_ != 0 &&
            (weight > weight_capacity_ ||
             reserved_weight_ > weight_capacity_ - weight);
        if (stopping_ || queue_.size() >= queue_capacity_ ||
            weight_exceeded) {
            return false;
        }
        queue_.push(WeightedTask{std::move(task), weight});
        reserved_weight_ += weight;
    }
    wake_workers_.notify_one();
    return true;
}

std::size_t BoundedThreadPool::reserved_weight() const {
    std::scoped_lock lock(mutex_);
    return reserved_weight_;
}

void BoundedThreadPool::shutdown() {
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
    }
    wake_workers_.notify_all();
    workers_.clear();
}

void BoundedThreadPool::worker_loop() {
    while (true) {
        WeightedTask task;
        {
            std::unique_lock lock(mutex_);
            wake_workers_.wait(lock, [this] {
                return stopping_ || !queue_.empty();
            });
            if (queue_.empty()) {
                if (stopping_) {
                    return;
                }
                continue;
            }
            task = std::move(queue_.front());
            queue_.pop();
        }
        try {
            task.action();
        } catch (const std::exception&) {
            // Job boundaries log their own failures. This final guard prevents
            // an unexpected task exception from terminating the worker.
        } catch (...) {
        }
        {
            std::scoped_lock lock(mutex_);
            reserved_weight_ -= task.weight;
        }
    }
}

} // namespace pfh::infrastructure
