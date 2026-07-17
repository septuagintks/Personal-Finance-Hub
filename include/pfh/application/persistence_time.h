// Personal Finance Hub - Persistence Time Precision

#pragma once

#include <chrono>

namespace pfh::application {

/// PostgreSQL TIMESTAMPTZ and the persistence adapters retain microseconds.
/// Normalize before constructing a response-producing domain object so the
/// first response matches later reads from PostgreSQL.
[[nodiscard]] inline std::chrono::system_clock::time_point
normalize_persisted_time(
    std::chrono::system_clock::time_point value) noexcept {
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        value.time_since_epoch());
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(micros));
}

} // namespace pfh::application
