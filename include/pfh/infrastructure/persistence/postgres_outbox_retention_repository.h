// Personal Finance Hub - PostgreSQL Published Outbox Retention

#pragma once

#include "pfh/application/maintenance/i_outbox_retention_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class PostgresOutboxRetentionRepository final
    : public application::IOutboxRetentionRepository {
public:
    explicit PostgresOutboxRetentionRepository(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<std::size_t> cleanup_published(
        std::chrono::system_clock::time_point now,
        std::chrono::seconds retention,
        std::size_t batch_size) override;

private:
    drogon::orm::DbClientPtr db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
