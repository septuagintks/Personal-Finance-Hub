// Personal Finance Hub - In-Memory Published Outbox Retention

#pragma once

#include "pfh/application/maintenance/i_outbox_retention_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace pfh::infrastructure {

class InMemoryOutboxRetentionRepository final
    : public application::IOutboxRetentionRepository {
public:
    explicit InMemoryOutboxRetentionRepository(InMemoryStore& store)
        : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<std::size_t> cleanup_published(
        std::chrono::system_clock::time_point now,
        std::chrono::seconds retention,
        std::size_t batch_size) override {
        if (retention < std::chrono::hours(24) ||
            retention > std::chrono::hours(24 * 3650) ||
            batch_size == 0 || batch_size > 10000) {
            return std::unexpected(domain::RepositoryError::validation(
                "Outbox retention request is invalid"));
        }

        std::scoped_lock lock(store_.mutex);
        const auto cutoff = now - retention;
        std::vector<const OutboxRecord*> eligible;
        eligible.reserve(std::min(batch_size, store_.outbox.size()));
        for (const auto& message : store_.outbox) {
            if (message.status == application::OutboxStatus::Published &&
                message.published_at !=
                    std::chrono::system_clock::time_point{} &&
                message.published_at < cutoff) {
                eligible.push_back(&message);
            }
        }
        std::ranges::sort(eligible, [](const auto* left, const auto* right) {
            if (left->created_at != right->created_at) {
                return left->created_at < right->created_at;
            }
            return left->id < right->id;
        });
        if (eligible.size() > batch_size) {
            eligible.resize(batch_size);
        }

        std::set<std::string> ids;
        for (const auto* message : eligible) {
            ids.insert(message->id);
        }
        if (ids.empty()) return std::size_t{0};

        std::erase_if(store_.outbox_retry_commands, [&](const auto& entry) {
            return ids.contains(entry.second.outbox_id);
        });
        std::erase_if(store_.outbox_handler_receipts, [&](const auto& receipt) {
            return ids.contains(receipt.first);
        });
        std::erase_if(store_.outbox, [&](const auto& message) {
            return ids.contains(message.id);
        });
        return ids.size();
    }

private:
    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
