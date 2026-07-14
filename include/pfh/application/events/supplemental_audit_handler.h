// Personal Finance Hub - Supplemental System Audit Event Handler

#pragma once

#include "pfh/application/events/i_event_bus.h"
#include "pfh/application/events/i_supplemental_audit_store.h"

namespace pfh::application {

class SupplementalAuditHandler final
    : public IEventHandler,
      public IDeadLetterHandler {
public:
    explicit SupplementalAuditHandler(ISupplementalAuditStore& store)
        : store_(store) {}

    [[nodiscard]] std::string_view handler_name() const noexcept override;
    [[nodiscard]] std::string_view dead_letter_handler_name()
        const noexcept override;
    [[nodiscard]] bool handles(
        std::string_view event_name) const noexcept override;
    [[nodiscard]] EventHandlingResult handle(
        const OutboxMessage& message) override;
    [[nodiscard]] EventHandlingResult handle_dead_letter(
        const OutboxMessage& message) override;

private:
    [[nodiscard]] EventHandlingResult append(
        const OutboxMessage& message,
        std::string_view receipt_name,
        domain::AuditAction action,
        std::string resource_type,
        std::string metadata_json);

    ISupplementalAuditStore& store_;
};

} // namespace pfh::application
