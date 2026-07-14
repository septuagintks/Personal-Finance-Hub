// Personal Finance Hub - Process-local Event Bus

#pragma once

#include "pfh/application/events/i_event_bus.h"

#include <mutex>
#include <vector>

namespace pfh::application {

class LocalEventBus final : public IEventBus {
public:
    void subscribe(std::shared_ptr<IEventHandler> handler) override;
    [[nodiscard]] EventHandlingResult publish(
        const OutboxMessage& message) override;

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<IEventHandler>> handlers_;
};

} // namespace pfh::application
