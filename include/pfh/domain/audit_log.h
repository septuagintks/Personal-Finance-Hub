// Personal Finance Hub - Audit Log Model

#pragma once

#include "pfh/domain/user.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pfh::domain {

enum class AuditAction {
    Create,
    Update,
    Archive,
    Delete,
    DangerousDelete,
    SyncImport,
    Refresh,
    Register,
    Login,
    Logout,
    TokenRefresh,
    SecurityEvent,
    Retry
};

enum class AuditActorType {
    User,
    Operator,
    System
};

struct AuditLogEntry {
    std::int64_t id = 0;
    std::optional<UserId> operator_user_id;
    AuditActorType actor_type = AuditActorType::User;
    AuditAction action = AuditAction::Create;
    std::string resource_type;
    std::string resource_id;
    std::string before_value_json;
    std::string after_value_json;
    std::string metadata_json;
    std::string trace_id;
    std::chrono::system_clock::time_point occurred_at{};
};

struct UserAuditLogQuery {
    UserId user_id;
    std::optional<AuditAction> action;
    std::optional<std::string> resource_type;
    std::optional<std::chrono::system_clock::time_point> from;
    std::optional<std::chrono::system_clock::time_point> to;
    std::optional<std::int64_t> before_id;
    std::size_t limit = 50;
};

struct UserAuditLogPage {
    std::vector<AuditLogEntry> entries;
    std::optional<std::int64_t> next_before_id;
};

} // namespace pfh::domain
