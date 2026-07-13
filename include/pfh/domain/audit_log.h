// Personal Finance Hub - Audit Log Model

#pragma once

#include "pfh/domain/user.h"

#include <chrono>
#include <string>

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
    SecurityEvent
};

struct AuditLogEntry {
    UserId operator_user_id;
    AuditAction action = AuditAction::Create;
    std::string resource_type;
    std::string resource_id;
    std::string before_value_json;
    std::string after_value_json;
    std::string metadata_json;
    std::chrono::system_clock::time_point occurred_at{};
};

} // namespace pfh::domain
