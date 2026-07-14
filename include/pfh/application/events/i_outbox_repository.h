// Personal Finance Hub - Transactional Outbox Repository Port

#pragma once

#include "pfh/application/events/outbox_message.h"
#include "pfh/domain/repositories/repository_error.h"

#include <chrono>
#include <cstddef>
#include <string_view>

namespace pfh::application {

class IOutboxRepository {
public:
    virtual ~IOutboxRepository() = default;

    [[nodiscard]] virtual domain::RepositoryResult<OutboxClaimBatch> claim_due(
        std::chrono::system_clock::time_point now,
        std::chrono::seconds processing_timeout,
        std::size_t batch_size,
        std::string_view worker_id) = 0;

    [[nodiscard]] virtual domain::RepositoryVoidResult mark_published(
        std::string_view outbox_id,
        std::string_view claim_token,
        std::chrono::system_clock::time_point published_at) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<OutboxFailureTransition>
    mark_failed(
        std::string_view outbox_id,
        std::string_view claim_token,
        std::string_view handler_name,
        std::string_view error_summary,
        std::chrono::system_clock::time_point failed_at,
        std::chrono::system_clock::time_point next_retry_at) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<std::vector<OutboxMessage>>
    list_unhandled_dead_letters(
        std::string_view handler_name,
        std::size_t limit) = 0;
};

} // namespace pfh::application
