// Personal Finance Hub - Local Event Bus Port

#pragma once

#include "pfh/application/events/outbox_message.h"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace pfh::application {

struct EventHandlingError {
    std::string summary;
    std::string handler_name;
};

using EventHandlingResult = std::expected<void, EventHandlingError>;

class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    [[nodiscard]] virtual std::string_view handler_name() const noexcept = 0;
    [[nodiscard]] virtual bool handles(std::string_view event_name) const noexcept = 0;
    [[nodiscard]] virtual EventHandlingResult handle(
        const OutboxMessage& message) = 0;
};

class IDeadLetterHandler {
public:
    virtual ~IDeadLetterHandler() = default;
    [[nodiscard]] virtual std::string_view dead_letter_handler_name()
        const noexcept = 0;
    [[nodiscard]] virtual EventHandlingResult handle_dead_letter(
        const OutboxMessage& message) = 0;
};

class IEventBus {
public:
    virtual ~IEventBus() = default;
    virtual void subscribe(std::shared_ptr<IEventHandler> handler) = 0;
    [[nodiscard]] virtual EventHandlingResult publish(
        const OutboxMessage& message) = 0;
};

} // namespace pfh::application
