// Personal Finance Hub - Data-Minimized Operational Read Models

#pragma once

#include "pfh/domain/user.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pfh::application {

enum class JobLastResult {
    NeverRun,
    Succeeded,
    Failed,
    Skipped
};

struct JobRuntimeSnapshot {
    std::string name;
    bool scheduler_started = false;
    bool running = false;
    std::uint64_t execution_sequence = 0;
    JobLastResult last_result = JobLastResult::NeverRun;
    std::optional<std::chrono::system_clock::time_point> last_started_at;
    std::optional<std::chrono::system_clock::time_point> last_finished_at;
    std::int64_t last_duration_milliseconds = 0;
};

struct OperationalLeaseSummary {
    std::string job_name;
    bool active = false;
    std::chrono::system_clock::time_point lease_until{};
};

struct OperationalDataSummary {
    std::map<std::string, std::size_t> outbox_counts;
    std::size_t handler_receipt_count = 0;
    std::optional<std::chrono::system_clock::time_point> latest_receipt_at;
    std::size_t expired_idempotency_count = 0;
    std::vector<OperationalLeaseSummary> leases;
};

struct OperationalOverview {
    OperationalDataSummary data;
    std::vector<JobRuntimeSnapshot> jobs;
    std::chrono::system_clock::time_point generated_at{};
};

struct DeadLetterSummary {
    std::string id;
    std::string event_name;
    std::string aggregate_type;
    std::string aggregate_id;
    int retry_count = 0;
    int max_retry_count = 0;
    std::string last_failed_handler;
    std::chrono::system_clock::time_point last_failed_at{};
    std::chrono::system_clock::time_point created_at{};
};

struct DeadLetterPage {
    std::vector<DeadLetterSummary> items;
    std::optional<std::string> next_cursor;
};

struct RetryDeadLetterCommand {
    domain::UserId operator_user_id;
    std::string outbox_id;
    std::string idempotency_key;
    std::string trace_id;
    std::chrono::system_clock::time_point requested_at{};
};

struct RetryDeadLetterResult {
    std::string outbox_id;
    bool replayed = false;
};

struct ReadinessState {
    bool database_ready = false;
    bool migrations_current = false;
};

} // namespace pfh::application
