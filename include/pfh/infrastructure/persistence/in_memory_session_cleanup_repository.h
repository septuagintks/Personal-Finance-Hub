// Personal Finance Hub - In-Memory Authentication Data Cleanup

#pragma once

#include "pfh/application/maintenance/i_session_cleanup_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <cstddef>
#include <mutex>

namespace pfh::infrastructure {

class InMemorySessionCleanupRepository final
    : public application::ISessionCleanupRepository {
public:
    explicit InMemorySessionCleanupRepository(InMemoryStore& store)
        : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<application::SessionCleanupSummary>
    delete_expired(
        std::chrono::system_clock::time_point now,
        std::size_t batch_size) override {
        if (batch_size == 0) {
            return std::unexpected(domain::RepositoryError::validation(
                "Session cleanup batch size must be positive"));
        }
        std::scoped_lock lock(store_.mutex);
        application::SessionCleanupSummary summary;
        summary.refresh_tokens_deleted = erase_expired(
            store_.refresh_tokens,
            now,
            batch_size,
            [](const auto& record) { return record.expires_at; });
        summary.revoked_access_tokens_deleted = erase_expired(
            store_.revoked_access_tokens,
            now,
            batch_size,
            [](const auto& record) { return record.expires_at; });
        summary.revoked_sessions_deleted = erase_expired(
            store_.revoked_sessions,
            now,
            batch_size,
            [](const auto& record) { return record.expires_at; });
        return summary;
    }

private:
    template <typename Map, typename Expiry>
    static std::size_t erase_expired(
        Map& records,
        std::chrono::system_clock::time_point now,
        std::size_t limit,
        Expiry&& expiry) {
        std::size_t removed = 0;
        for (auto iterator = records.begin();
             iterator != records.end() && removed < limit;) {
            if (expiry(iterator->second) > now) {
                ++iterator;
                continue;
            }
            iterator = records.erase(iterator);
            ++removed;
        }
        return removed;
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
