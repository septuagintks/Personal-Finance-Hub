// Personal Finance Hub - Bounded authentication rate limiter

#include "pfh/presentation/http/auth_rate_limiter.h"

#include <stdexcept>

namespace pfh::presentation {

AuthRateLimiter::AuthRateLimiter(
    std::uint32_t maximum_attempts,
    std::chrono::seconds window,
    std::size_t maximum_sources)
    : maximum_attempts_(maximum_attempts),
      window_(window),
      maximum_sources_(maximum_sources) {
    if (maximum_attempts_ == 0 ||
        window_ <= std::chrono::seconds::zero() ||
        maximum_sources_ == 0) {
        throw std::invalid_argument("Authentication rate limit must be positive");
    }
}

bool AuthRateLimiter::allow(
    std::string_view source,
    Clock::time_point now) {
    const std::string key = source.empty() ? "unknown" : std::string(source);
    std::scoped_lock lock(mutex_);

    auto found = counters_.find(key);
    if (found == counters_.end()) {
        if (counters_.size() >= maximum_sources_) {
            prune_expired(now);
        }
        if (counters_.size() >= maximum_sources_) {
            return false;
        }
        found = counters_.emplace(
            key, Counter{now, 0}).first;
    }

    auto& counter = found->second;
    if (now - counter.window_started_at >= window_) {
        counter.window_started_at = now;
        counter.attempts = 0;
    }
    if (counter.attempts >= maximum_attempts_) {
        return false;
    }
    ++counter.attempts;
    return true;
}

std::size_t AuthRateLimiter::tracked_sources() const {
    std::scoped_lock lock(mutex_);
    return counters_.size();
}

void AuthRateLimiter::prune_expired(Clock::time_point now) {
    for (auto current = counters_.begin(); current != counters_.end();) {
        if (now - current->second.window_started_at >= window_) {
            current = counters_.erase(current);
        } else {
            ++current;
        }
    }
}

} // namespace pfh::presentation
