// Personal Finance Hub - Process-local Event Bus

#include "pfh/application/events/local_event_bus.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace pfh::application {

void LocalEventBus::subscribe(std::shared_ptr<IEventHandler> handler) {
    if (!handler) {
        return;
    }
    std::scoped_lock lock(mutex_);
    if (handler->handler_name().empty() ||
        std::ranges::any_of(handlers_, [&](const auto& registered) {
            return registered &&
                   registered->handler_name() == handler->handler_name();
        })) {
        return;
    }
    handlers_.push_back(std::move(handler));
}

EventHandlingResult LocalEventBus::publish(const OutboxMessage& message) {
    std::vector<std::shared_ptr<IEventHandler>> snapshot;
    {
        std::scoped_lock lock(mutex_);
        snapshot = handlers_;
    }

    for (const auto& handler : snapshot) {
        if (!handler || !handler->handles(message.event_name)) {
            continue;
        }
        try {
            auto handled = handler->handle(message);
            if (!handled) {
                auto error = std::move(handled.error());
                if (error.handler_name.empty()) {
                    error.handler_name = std::string(handler->handler_name());
                }
                return std::unexpected(std::move(error));
            }
        } catch (const std::exception&) {
            return std::unexpected(EventHandlingError{
                "event handler raised an exception",
                std::string(handler->handler_name())});
        } catch (...) {
            return std::unexpected(EventHandlingError{
                "event handler raised an unknown exception",
                std::string(handler->handler_name())});
        }
    }
    return {};
}

} // namespace pfh::application
