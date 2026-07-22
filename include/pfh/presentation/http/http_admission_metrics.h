// Personal Finance Hub - HTTP Admission Capacity and Rejection Metrics

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pfh::presentation {

struct HttpAdmissionCapacity {
    std::size_t maximum_request_body_bytes = 0;
    std::size_t request_worker_threads = 0;
    std::size_t request_queue_capacity = 0;
    std::size_t request_queue_byte_capacity = 0;
    std::size_t auth_worker_threads = 0;
    std::size_t auth_queue_capacity = 0;
    std::size_t auth_queue_byte_capacity = 0;
    std::size_t auth_rate_limit_attempts = 0;
    std::size_t auth_rate_limit_window_seconds = 0;
    std::size_t auth_rate_limit_sources = 0;
};

struct HttpAdmissionSnapshot {
    HttpAdmissionCapacity capacity;
    std::uint64_t oversized_body_rejections = 0;
    std::uint64_t auth_rate_limit_rejections = 0;
    std::uint64_t request_queue_rejections = 0;
    std::uint64_t auth_queue_rejections = 0;
};

class HttpAdmissionMetrics {
public:
    explicit HttpAdmissionMetrics(HttpAdmissionCapacity capacity)
        : capacity_(capacity) {}

    void record_oversized_body_rejection() noexcept {
        oversized_body_rejections_.fetch_add(1, std::memory_order_relaxed);
    }
    void record_auth_rate_limit_rejection() noexcept {
        auth_rate_limit_rejections_.fetch_add(1, std::memory_order_relaxed);
    }
    void record_request_queue_rejection() noexcept {
        request_queue_rejections_.fetch_add(1, std::memory_order_relaxed);
    }
    void record_auth_queue_rejection() noexcept {
        auth_queue_rejections_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] HttpAdmissionSnapshot snapshot() const noexcept {
        return HttpAdmissionSnapshot{
            capacity_,
            oversized_body_rejections_.load(std::memory_order_relaxed),
            auth_rate_limit_rejections_.load(std::memory_order_relaxed),
            request_queue_rejections_.load(std::memory_order_relaxed),
            auth_queue_rejections_.load(std::memory_order_relaxed)};
    }

private:
    const HttpAdmissionCapacity capacity_;
    std::atomic<std::uint64_t> oversized_body_rejections_{0};
    std::atomic<std::uint64_t> auth_rate_limit_rejections_{0};
    std::atomic<std::uint64_t> request_queue_rejections_{0};
    std::atomic<std::uint64_t> auth_queue_rejections_{0};
};

} // namespace pfh::presentation
