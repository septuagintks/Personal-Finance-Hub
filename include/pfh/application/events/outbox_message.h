// Personal Finance Hub - Transactional Outbox Message

#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace pfh::application {

enum class OutboxStatus {
    Pending,
    Processing,
    Published,
    Failed,
    DeadLetter
};

struct OutboxMessage {
    std::string id;
    std::string event_name;
    std::string aggregate_type;
    std::string aggregate_id;
    std::string payload_json;
    OutboxStatus status = OutboxStatus::Pending;
    int retry_count = 0;
    int max_retry_count = 5;
    std::chrono::system_clock::time_point next_retry_at{};
    std::string last_error;
    std::string last_failed_handler;
    std::chrono::system_clock::time_point last_failed_at{};
    std::chrono::system_clock::time_point occurred_at{};
    std::chrono::system_clock::time_point created_at{};
    std::chrono::system_clock::time_point published_at{};
    std::chrono::system_clock::time_point locked_at{};
    std::string locked_by;
    std::string claim_token;
};

struct OutboxClaimBatch {
    std::vector<OutboxMessage> claimed;
    std::vector<OutboxMessage> recovered_dead_letters;
};

enum class OutboxFailureDisposition {
    RetryScheduled,
    DeadLettered
};

struct OutboxFailureTransition {
    OutboxFailureDisposition disposition =
        OutboxFailureDisposition::RetryScheduled;
    int retry_count = 0;
};

struct OutboxPublishSummary {
    std::size_t claimed = 0;
    std::size_t published = 0;
    std::size_t failed = 0;
    std::size_t dead_lettered = 0;
    std::size_t dead_letters_audited = 0;
    std::size_t audit_failures = 0;
};

} // namespace pfh::application
