// Personal Finance Hub - In-Memory Idempotency Cleanup Adapter

#pragma once

#include "pfh/application/maintenance/i_idempotency_cleanup_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace pfh::infrastructure {

class InMemoryIdempotencyCleanupRepository final
    : public application::IIdempotencyCleanupRepository {
public:
    explicit InMemoryIdempotencyCleanupRepository(InMemoryStore& store)
        : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<std::size_t> cleanup_expired(
        std::chrono::system_clock::time_point now,
        std::size_t batch_size) override {
        if (batch_size == 0 || batch_size > 10000) {
            return std::unexpected(domain::RepositoryError::validation(
                "Idempotency cleanup batch is invalid"));
        }
        std::scoped_lock lock(store_.mutex);
        std::vector<std::pair<std::string, std::chrono::system_clock::time_point>>
            expired;
        for (const auto& [key, record] : store_.idempotency) {
            if (record.expires_at <= now) expired.emplace_back(key, record.expires_at);
        }
        std::ranges::sort(expired, [](const auto& left, const auto& right) {
            if (left.second != right.second) return left.second < right.second;
            return left.first < right.first;
        });
        const auto count = std::min(batch_size, expired.size());
        for (std::size_t index = 0; index < count; ++index) {
            store_.idempotency.erase(expired[index].first);
        }
        return count;
    }

private:
    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
