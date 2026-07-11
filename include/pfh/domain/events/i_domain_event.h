// Personal Finance Hub - Domain Event Interface
// Version: 1.0
// C++23
//
// Domain events are immutable past-tense facts. Payload is intentionally
// minimal (IDs + timestamps + short summaries) so outbox storage stays small.

#pragma once

#include <chrono>
#include <string>

namespace pfh::domain {

class IDomainEvent {
public:
    virtual ~IDomainEvent() = default;

    [[nodiscard]] virtual std::string event_name() const = 0;
    [[nodiscard]] virtual std::chrono::system_clock::time_point occurred_at() const = 0;
    [[nodiscard]] virtual std::string aggregate_type() const = 0;
    [[nodiscard]] virtual std::string aggregate_id() const = 0;
    [[nodiscard]] virtual std::string payload_json() const = 0;
};

} // namespace pfh::domain
