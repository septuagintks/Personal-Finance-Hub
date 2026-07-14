// Personal Finance Hub - Transactional Outbox Publisher

#pragma once

#include "pfh/application/events/i_event_bus.h"
#include "pfh/application/events/i_outbox_repository.h"
#include "pfh/application/ports/i_clock.h"

#include <chrono>
#include <cstddef>
#include <string>

namespace pfh::application {

struct OutboxPublisherConfig {
    std::size_t batch_size = 100;
    std::chrono::seconds processing_timeout{300};
    std::size_t dead_letter_audit_batch_size = 100;
};

class OutboxPublisher {
public:
    OutboxPublisher(
        IOutboxRepository& outbox,
        IEventBus& event_bus,
        IClock& clock,
        OutboxPublisherConfig config = {},
        IDeadLetterHandler* dead_letter_handler = nullptr)
        : outbox_(outbox),
          event_bus_(event_bus),
          clock_(clock),
          config_(config),
          dead_letter_handler_(dead_letter_handler) {}

    [[nodiscard]] domain::RepositoryResult<OutboxPublishSummary> run_once(
        std::string worker_id);

private:
    [[nodiscard]] std::chrono::seconds retry_delay(int retry_count) const;
    void audit_dead_letter(
        const OutboxMessage& message,
        OutboxPublishSummary& summary);
    [[nodiscard]] domain::RepositoryVoidResult audit_unhandled_dead_letters(
        OutboxPublishSummary& summary);

    IOutboxRepository& outbox_;
    IEventBus& event_bus_;
    IClock& clock_;
    OutboxPublisherConfig config_;
    IDeadLetterHandler* dead_letter_handler_;
};

} // namespace pfh::application
