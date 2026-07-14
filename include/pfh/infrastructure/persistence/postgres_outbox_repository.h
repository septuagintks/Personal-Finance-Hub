// Personal Finance Hub - PostgreSQL Transactional Outbox Repository

#pragma once

#include "pfh/application/events/i_outbox_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class PostgresOutboxRepository final
    : public application::IOutboxRepository {
public:
    explicit PostgresOutboxRepository(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<application::OutboxClaimBatch>
    claim_due(
        std::chrono::system_clock::time_point now,
        std::chrono::seconds processing_timeout,
        std::size_t batch_size,
        std::string_view worker_id) override;

    [[nodiscard]] domain::RepositoryVoidResult mark_published(
        std::string_view outbox_id,
        std::string_view claim_token,
        std::chrono::system_clock::time_point published_at) override;

    [[nodiscard]] domain::RepositoryResult<application::OutboxFailureTransition>
    mark_failed(
        std::string_view outbox_id,
        std::string_view claim_token,
        std::string_view handler_name,
        std::string_view error_summary,
        std::chrono::system_clock::time_point failed_at,
        std::chrono::system_clock::time_point next_retry_at) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<application::OutboxMessage>>
    list_unhandled_dead_letters(
        std::string_view handler_name,
        std::size_t limit) override;

private:
    drogon::orm::DbClientPtr db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
