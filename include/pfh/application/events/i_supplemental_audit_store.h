// Personal Finance Hub - Idempotent Supplemental Audit Store Port

#pragma once

#include "pfh/domain/audit_log.h"
#include "pfh/domain/repositories/repository_error.h"

#include <string_view>

namespace pfh::application {

class ISupplementalAuditStore {
public:
    virtual ~ISupplementalAuditStore() = default;

    // Returns true when the audit was appended. A false success means the
    // outbox_id + handler_name receipt already existed.
    [[nodiscard]] virtual domain::RepositoryResult<bool> append_once(
        std::string_view outbox_id,
        std::string_view handler_name,
        const domain::AuditLogEntry& entry) = 0;
};

} // namespace pfh::application
