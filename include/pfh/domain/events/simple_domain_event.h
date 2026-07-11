// Personal Finance Hub - Simple Domain Event for Repository Tests
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/events/i_domain_event.h"
#include <chrono>
#include <string>
#include <utility>

namespace pfh::domain {

class SimpleDomainEvent final : public IDomainEvent {
public:
    SimpleDomainEvent(
        std::string event_name,
        std::string aggregate_type,
        std::string aggregate_id,
        std::string payload_json,
        std::chrono::system_clock::time_point occurred_at = std::chrono::system_clock::now())
        : event_name_(std::move(event_name)),
          aggregate_type_(std::move(aggregate_type)),
          aggregate_id_(std::move(aggregate_id)),
          payload_json_(std::move(payload_json)),
          occurred_at_(occurred_at) {}

    [[nodiscard]] std::string event_name() const override { return event_name_; }
    [[nodiscard]] std::chrono::system_clock::time_point occurred_at() const override {
        return occurred_at_;
    }
    [[nodiscard]] std::string aggregate_type() const override { return aggregate_type_; }
    [[nodiscard]] std::string aggregate_id() const override { return aggregate_id_; }
    [[nodiscard]] std::string payload_json() const override { return payload_json_; }

private:
    std::string event_name_;
    std::string aggregate_type_;
    std::string aggregate_id_;
    std::string payload_json_;
    std::chrono::system_clock::time_point occurred_at_;
};

} // namespace pfh::domain
