// Personal Finance Hub - Bounded authentication rate limiter

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace pfh::presentation {

class AuthRateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    AuthRateLimiter(
        std::uint32_t maximum_attempts,
        std::chrono::seconds window,
        std::size_t maximum_sources);

    [[nodiscard]] bool allow(
        std::string_view source,
        Clock::time_point now = Clock::now());

    [[nodiscard]] std::size_t tracked_sources() const;

private:
    struct Counter {
        Clock::time_point window_started_at{};
        std::uint32_t attempts = 0;
    };

    void prune_expired(Clock::time_point now);

    std::uint32_t maximum_attempts_;
    std::chrono::seconds window_;
    std::size_t maximum_sources_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Counter> counters_;
};

} // namespace pfh::presentation
