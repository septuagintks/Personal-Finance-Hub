// Personal Finance Hub - In-Memory Transactional Outbox Repository

#pragma once

#include "pfh/application/events/i_outbox_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <algorithm>
#include <mutex>
#include <string>

namespace pfh::infrastructure {

class InMemoryOutboxRepository final
    : public application::IOutboxRepository {
public:
    explicit InMemoryOutboxRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<application::OutboxClaimBatch>
    claim_due(
        std::chrono::system_clock::time_point now,
        std::chrono::seconds processing_timeout,
        std::size_t batch_size,
        std::string_view worker_id) override {
        if (processing_timeout <= std::chrono::seconds::zero() ||
            batch_size == 0 || worker_id.empty() || worker_id.size() > 128) {
            return std::unexpected(domain::RepositoryError::validation(
                "Invalid outbox claim request"));
        }

        std::scoped_lock lock(store_.mutex);
        application::OutboxClaimBatch batch;
        const auto stale_before = now - processing_timeout;

        for (auto& message : store_.outbox) {
            if (message.status != application::OutboxStatus::Processing ||
                message.locked_at > stale_before) {
                continue;
            }
            ++message.retry_count;
            message.last_error = "processing lease expired";
            message.last_failed_handler = "outbox-lease";
            message.last_failed_at = now;
            clear_claim(message);
            if (message.retry_count >= message.max_retry_count) {
                message.status = application::OutboxStatus::DeadLetter;
                batch.recovered_dead_letters.push_back(message);
            } else {
                message.status = application::OutboxStatus::Failed;
                message.next_retry_at = now;
            }
        }

        for (auto& message : store_.outbox) {
            if (batch.claimed.size() >= batch_size) {
                break;
            }
            const bool due =
                (message.status == application::OutboxStatus::Pending ||
                 message.status == application::OutboxStatus::Failed) &&
                message.next_retry_at <= now;
            if (!due) {
                continue;
            }
            message.status = application::OutboxStatus::Processing;
            message.locked_at = now;
            message.locked_by = std::string(worker_id);
            message.claim_token = "claim-" +
                std::to_string(store_.next_outbox_claim_token++);
            batch.claimed.push_back(message);
        }
        return batch;
    }

    [[nodiscard]] domain::RepositoryVoidResult mark_published(
        std::string_view outbox_id,
        std::string_view claim_token,
        std::chrono::system_clock::time_point published_at) override {
        std::scoped_lock lock(store_.mutex);
        auto* message = find_claimed(outbox_id, claim_token);
        if (message == nullptr) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Outbox claim is no longer owned"));
        }
        message->status = application::OutboxStatus::Published;
        message->published_at = published_at;
        clear_claim(*message);
        return {};
    }

    [[nodiscard]] domain::RepositoryResult<application::OutboxFailureTransition>
    mark_failed(
        std::string_view outbox_id,
        std::string_view claim_token,
        std::string_view handler_name,
        std::string_view error_summary,
        std::chrono::system_clock::time_point failed_at,
        std::chrono::system_clock::time_point next_retry_at) override {
        if (outbox_id.empty() || claim_token.empty() ||
            handler_name.empty() || handler_name.size() > 128 ||
            error_summary.empty() || error_summary.size() > 1024) {
            return std::unexpected(domain::RepositoryError::validation(
                "Invalid outbox failure transition"));
        }
        std::scoped_lock lock(store_.mutex);
        auto* message = find_claimed(outbox_id, claim_token);
        if (message == nullptr) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Outbox claim is no longer owned"));
        }
        ++message->retry_count;
        message->last_error = std::string(error_summary);
        message->last_failed_handler = std::string(handler_name);
        message->last_failed_at = failed_at;
        clear_claim(*message);

        application::OutboxFailureTransition transition;
        transition.retry_count = message->retry_count;
        if (message->retry_count >= message->max_retry_count) {
            message->status = application::OutboxStatus::DeadLetter;
            transition.disposition =
                application::OutboxFailureDisposition::DeadLettered;
        } else {
            message->status = application::OutboxStatus::Failed;
            message->next_retry_at = next_retry_at;
            transition.disposition =
                application::OutboxFailureDisposition::RetryScheduled;
        }
        return transition;
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<application::OutboxMessage>>
    list_unhandled_dead_letters(
        std::string_view handler_name,
        std::size_t limit) override {
        if (handler_name.empty() || handler_name.size() > 128 || limit == 0) {
            return std::vector<application::OutboxMessage>{};
        }
        std::scoped_lock lock(store_.mutex);
        std::vector<application::OutboxMessage> result;
        for (const auto& message : store_.outbox) {
            if (message.status != application::OutboxStatus::DeadLetter ||
                store_.outbox_handler_receipts.contains(
                    {message.id, std::string(handler_name)})) {
                continue;
            }
            result.push_back(message);
            if (result.size() >= limit) {
                break;
            }
        }
        return result;
    }

private:
    [[nodiscard]] application::OutboxMessage* find_claimed(
        std::string_view outbox_id,
        std::string_view claim_token) {
        const auto found = std::ranges::find_if(
            store_.outbox,
            [&](const auto& message) {
                return message.id == outbox_id &&
                       message.status == application::OutboxStatus::Processing &&
                       message.claim_token == claim_token;
            });
        return found == store_.outbox.end() ? nullptr : &*found;
    }

    static void clear_claim(application::OutboxMessage& message) {
        message.locked_at = {};
        message.locked_by.clear();
        message.claim_token.clear();
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
