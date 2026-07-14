// Personal Finance Hub - PostgreSQL Idempotent Supplemental Audit Store

#pragma once

#include "pfh/application/events/i_supplemental_audit_store.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class PostgresSupplementalAuditStore final
    : public application::ISupplementalAuditStore {
public:
    explicit PostgresSupplementalAuditStore(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<bool> append_once(
        std::string_view outbox_id,
        std::string_view handler_name,
        const domain::AuditLogEntry& entry) override;

private:
    drogon::orm::DbClientPtr db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
