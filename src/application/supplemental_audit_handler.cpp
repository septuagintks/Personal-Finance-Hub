// Personal Finance Hub - Supplemental System Audit Event Handler

#include "pfh/application/events/supplemental_audit_handler.h"

#include "pfh/domain/events/domain_events.h"

#include <chrono>
#include <string>

namespace pfh::application {

namespace {

constexpr std::string_view kHandlerName = "SupplementalAuditHandler";
constexpr std::string_view kDeadLetterHandlerName =
    "SupplementalAuditHandler.DeadLetter";

[[nodiscard]] std::string json_field(
    std::string_view name,
    std::string_view value) {
    return domain::event_detail::json_string(name) + ":" +
           domain::event_detail::json_string(value);
}

} // namespace

std::string_view SupplementalAuditHandler::handler_name() const noexcept {
    return kHandlerName;
}

std::string_view SupplementalAuditHandler::dead_letter_handler_name()
    const noexcept {
    return kDeadLetterHandlerName;
}

bool SupplementalAuditHandler::handles(
    std::string_view event_name) const noexcept {
    return event_name == "ExchangeRateRefreshed" ||
           event_name == "ExchangeRateRefreshFailed";
}

EventHandlingResult SupplementalAuditHandler::handle(
    const OutboxMessage& message) {
    const auto action = message.event_name == "ExchangeRateRefreshed"
                            ? domain::AuditAction::Refresh
                            : domain::AuditAction::SecurityEvent;
    const auto metadata = "{" + json_field("outboxId", message.id) + "," +
                          json_field("eventName", message.event_name) + "," +
                          json_field(
                              "status",
                              message.event_name == "ExchangeRateRefreshed"
                                  ? "published"
                                  : "provider_failed") + "}";
    return append(message, handler_name(), action, "ExchangeRate", metadata);
}

EventHandlingResult SupplementalAuditHandler::handle_dead_letter(
    const OutboxMessage& message) {
    const auto metadata = "{" + json_field("outboxId", message.id) + "," +
                          json_field("eventName", message.event_name) + "," +
                          json_field("status", "dead_letter") +
                          ",\"retryCount\":" +
                          std::to_string(message.retry_count) +
                          "," + json_field(
                              "handlerName",
                              message.last_failed_handler) +
                          "," + json_field("lastError", message.last_error) +
                          ",\"failedAtEpochSeconds\":" +
                          std::to_string(std::chrono::duration_cast<
                              std::chrono::seconds>(
                                  message.last_failed_at.time_since_epoch())
                                             .count()) +
                          "}";
    return append(
        message,
        dead_letter_handler_name(),
        domain::AuditAction::SecurityEvent,
        "DomainEventOutbox",
        metadata);
}

EventHandlingResult SupplementalAuditHandler::append(
    const OutboxMessage& message,
    std::string_view receipt_name,
    domain::AuditAction action,
    std::string resource_type,
    std::string metadata_json) {
    domain::AuditLogEntry entry;
    entry.actor_type = domain::AuditActorType::System;
    entry.operator_user_id = std::nullopt;
    entry.action = action;
    entry.resource_type = std::move(resource_type);
    entry.resource_id = message.aggregate_id.empty()
                            ? message.id
                            : message.aggregate_id;
    entry.metadata_json = std::move(metadata_json);
    entry.occurred_at = message.occurred_at;

    auto appended = store_.append_once(message.id, receipt_name, entry);
    if (!appended) {
        return std::unexpected(EventHandlingError{
            "supplemental audit persistence failed",
            std::string(receipt_name)});
    }
    return {};
}

} // namespace pfh::application
