// Personal Finance Hub - In-Memory Idempotent Supplemental Audit Store

#pragma once

#include "pfh/application/events/i_supplemental_audit_store.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <mutex>
#include <string>

namespace pfh::infrastructure {

class InMemorySupplementalAuditStore final
    : public application::ISupplementalAuditStore {
public:
    explicit InMemorySupplementalAuditStore(InMemoryStore& store)
        : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<bool> append_once(
        std::string_view outbox_id,
        std::string_view handler_name,
        const domain::AuditLogEntry& entry) override {
        if (outbox_id.empty() || handler_name.empty() ||
            handler_name.size() > 128 ||
            entry.actor_type != domain::AuditActorType::System ||
            entry.operator_user_id.has_value() ||
            entry.resource_type.empty() || entry.resource_id.empty()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Invalid supplemental audit record"));
        }
        std::scoped_lock lock(store_.mutex);
        const auto key = std::pair{
            std::string(outbox_id), std::string(handler_name)};
        if (store_.outbox_handler_receipts.contains(key)) {
            return false;
        }
        store_.audit_logs.push_back(entry);
        store_.outbox_handler_receipts.insert(key);
        return true;
    }

private:
    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
