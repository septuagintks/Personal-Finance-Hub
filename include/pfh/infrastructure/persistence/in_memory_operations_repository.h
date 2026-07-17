// Personal Finance Hub - In-Memory Operations Adapter

#pragma once

#include "pfh/application/operations/i_operations_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <algorithm>
#include <mutex>

namespace pfh::infrastructure {

class InMemoryOperationsRepository final
    : public application::IOperationsRepository {
public:
    explicit InMemoryOperationsRepository(InMemoryStore& store)
        : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<application::ReadinessState>
    readiness(std::int64_t expected_migration_version) override {
        return application::ReadinessState{
            true, expected_migration_version == 10};
    }

    [[nodiscard]] domain::RepositoryResult<application::OperationalDataSummary>
    summary(std::chrono::system_clock::time_point now) override {
        std::scoped_lock lock(store_.mutex);
        application::OperationalDataSummary result;
        result.outbox_counts = {
            {"pending", 0}, {"processing", 0}, {"published", 0},
            {"failed", 0}, {"deadLetter", 0}};
        for (const auto& message : store_.outbox) {
            switch (message.status) {
            case application::OutboxStatus::Pending:
                ++result.outbox_counts["pending"];
                break;
            case application::OutboxStatus::Processing:
                ++result.outbox_counts["processing"];
                break;
            case application::OutboxStatus::Published:
                ++result.outbox_counts["published"];
                break;
            case application::OutboxStatus::Failed:
                ++result.outbox_counts["failed"];
                break;
            case application::OutboxStatus::DeadLetter:
                ++result.outbox_counts["deadLetter"];
                break;
            }
        }
        result.handler_receipt_count = store_.outbox_handler_receipts.size();
        for (const auto& [name, lease] : store_.scheduled_job_leases) {
            result.leases.push_back(application::OperationalLeaseSummary{
                name, lease.lease_until > now, lease.lease_until});
        }
        for (const auto& [_, record] : store_.idempotency) {
            if (record.expires_at <= now) ++result.expired_idempotency_count;
        }
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<application::DeadLetterPage>
    list_dead_letters(
        std::optional<std::string_view> cursor,
        std::size_t limit) override {
        if (limit == 0 || limit > 100) {
            return std::unexpected(domain::RepositoryError::validation(
                "Dead-letter query is invalid"));
        }
        std::scoped_lock lock(store_.mutex);
        std::vector<const application::OutboxMessage*> dead_letters;
        for (const auto& message : store_.outbox) {
            if (message.status == application::OutboxStatus::DeadLetter) {
                dead_letters.push_back(&message);
            }
        }
        std::ranges::sort(dead_letters, [](const auto* left, const auto* right) {
            if (left->created_at != right->created_at) {
                return left->created_at > right->created_at;
            }
            return left->id > right->id;
        });
        std::size_t start = 0;
        if (cursor.has_value()) {
            const auto found = std::ranges::find_if(
                dead_letters,
                [&](const auto* message) { return message->id == *cursor; });
            if (found == dead_letters.end()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Dead-letter cursor is invalid"));
            }
            start = static_cast<std::size_t>(
                std::distance(dead_letters.begin(), found)) + 1;
        }
        application::DeadLetterPage page;
        const auto end = std::min(dead_letters.size(), start + limit);
        for (auto index = start; index < end; ++index) {
            const auto& message = *dead_letters[index];
            page.items.push_back(application::DeadLetterSummary{
                message.id,
                message.event_name,
                message.aggregate_type,
                message.aggregate_id,
                message.retry_count,
                message.max_retry_count,
                message.last_failed_handler,
                message.last_failed_at,
                message.created_at});
        }
        if (end < dead_letters.size() && !page.items.empty()) {
            page.next_cursor = page.items.back().id;
        }
        return page;
    }

    [[nodiscard]] domain::RepositoryResult<application::RetryDeadLetterResult>
    retry_dead_letter(
        const application::RetryDeadLetterCommand& command) override {
        if (!command.operator_user_id.is_valid() || command.outbox_id.empty() ||
            command.idempotency_key.empty() || command.idempotency_key.size() > 128 ||
            command.trace_id.empty() || command.trace_id.size() > 128) {
            return std::unexpected(domain::RepositoryError::validation(
                "Dead-letter retry request is invalid"));
        }
        std::scoped_lock lock(store_.mutex);
        const auto user = store_.users.find(command.operator_user_id.value());
        if (user == store_.users.end() ||
            user->second.user.role() != domain::UserRole::Operator) {
            return std::unexpected(domain::RepositoryError::validation(
                "Operator role is required"));
        }
        const auto command_key = command.operator_user_id.to_string() + ":" +
            command.idempotency_key;
        if (const auto existing = store_.outbox_retry_commands.find(command_key);
            existing != store_.outbox_retry_commands.end()) {
            if (existing->second.outbox_id != command.outbox_id) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Idempotency key was used for another dead letter"));
            }
            return application::RetryDeadLetterResult{
                command.outbox_id, true};
        }
        const auto message = std::ranges::find_if(
            store_.outbox,
            [&](const auto& value) { return value.id == command.outbox_id; });
        if (message == store_.outbox.end()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Dead letter not found"));
        }
        if (message->status != application::OutboxStatus::DeadLetter) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Outbox event is not dead-lettered"));
        }
        store_.outbox_retry_commands.emplace(
            command_key,
            InMemoryOutboxRetryCommand{
                command.operator_user_id,
                command.idempotency_key,
                command.outbox_id,
                command.trace_id,
                command.requested_at});
        message->status = application::OutboxStatus::Failed;
        message->retry_count = 0;
        message->next_retry_at = command.requested_at;
        message->last_error.clear();
        message->last_failed_handler.clear();
        message->last_failed_at = {};
        message->locked_at = {};
        message->locked_by.clear();
        message->claim_token.clear();

        domain::AuditLogEntry audit;
        audit.id = store_.next_audit_log_id++;
        audit.operator_user_id = command.operator_user_id;
        audit.actor_type = domain::AuditActorType::Operator;
        audit.action = domain::AuditAction::Retry;
        audit.resource_type = "Outbox";
        audit.resource_id = command.outbox_id;
        audit.metadata_json = "{}";
        audit.trace_id = command.trace_id;
        audit.occurred_at = command.requested_at;
        store_.audit_logs.push_back(std::move(audit));
        return application::RetryDeadLetterResult{
            command.outbox_id, false};
    }

private:
    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
