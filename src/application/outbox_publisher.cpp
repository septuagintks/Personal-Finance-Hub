// Personal Finance Hub - Transactional Outbox Publisher

#include "pfh/application/events/outbox_publisher.h"

#include <algorithm>
#include <array>
#include <exception>
#include <string_view>

namespace pfh::application {

namespace {

[[nodiscard]] std::string safe_error_summary(
    std::string_view summary) {
    constexpr std::size_t kMaxStoredErrorBytes = 1024;
    std::string result;
    result.reserve(std::min(summary.size(), kMaxStoredErrorBytes));
    for (const char raw : summary) {
        if (result.size() >= kMaxStoredErrorBytes) {
            break;
        }
        const auto c = static_cast<unsigned char>(raw);
        // Persist only printable ASCII. This prevents control-character log
        // injection and avoids splitting a UTF-8 sequence at the byte limit.
        if (c >= 0x20 && c <= 0x7e) {
            result.push_back(raw);
        }
    }
    if (result.empty()) {
        return "event handling failed";
    }
    return result;
}

[[nodiscard]] std::string safe_handler_name(std::string_view name) {
    constexpr std::size_t kMaxHandlerBytes = 128;
    std::string result;
    result.reserve(std::min(name.size(), kMaxHandlerBytes));
    for (const char raw : name) {
        if (result.size() >= kMaxHandlerBytes) {
            break;
        }
        const auto c = static_cast<unsigned char>(raw);
        if (c >= 0x21 && c <= 0x7e) {
            result.push_back(raw);
        }
    }
    return result.empty() ? "IEventBus" : result;
}

} // namespace

domain::RepositoryResult<OutboxPublishSummary> OutboxPublisher::run_once(
    std::string worker_id) {
    if (worker_id.empty() || config_.batch_size == 0 ||
        config_.processing_timeout <= std::chrono::seconds::zero()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Outbox publisher configuration is invalid"));
    }

    OutboxPublishSummary summary;
    if (auto audited = audit_unhandled_dead_letters(summary); !audited) {
        return std::unexpected(audited.error());
    }

    const auto now = clock_.now();
    auto batch = outbox_.claim_due(
        now,
        config_.processing_timeout,
        config_.batch_size,
        worker_id);
    if (!batch) {
        return std::unexpected(batch.error());
    }

    for (const auto& dead : batch->recovered_dead_letters) {
        ++summary.dead_lettered;
        audit_dead_letter(dead, summary);
    }

    summary.claimed = batch->claimed.size();
    for (const auto& message : batch->claimed) {
        EventHandlingResult handled;
        try {
            handled = event_bus_.publish(message);
        } catch (const std::exception&) {
            handled = std::unexpected(EventHandlingError{
                "event bus raised an exception", "IEventBus"});
        } catch (...) {
            handled = std::unexpected(EventHandlingError{
                "event bus raised an unknown exception", "IEventBus"});
        }

        if (handled) {
            auto marked = outbox_.mark_published(
                message.id, message.claim_token, clock_.now());
            if (!marked) {
                return std::unexpected(marked.error());
            }
            ++summary.published;
            continue;
        }

        const int next_retry_count = message.retry_count + 1;
        const auto failed_at = clock_.now();
        const auto error_summary = safe_error_summary(handled.error().summary);
        const auto handler_name = safe_handler_name(
            handled.error().handler_name);
        const auto next_retry_at =
            failed_at + retry_delay(next_retry_count);
        auto transition = outbox_.mark_failed(
            message.id,
            message.claim_token,
            handler_name,
            error_summary,
            failed_at,
            next_retry_at);
        if (!transition) {
            return std::unexpected(transition.error());
        }
        if (transition->disposition ==
            OutboxFailureDisposition::DeadLettered) {
            auto dead = message;
            dead.status = OutboxStatus::DeadLetter;
            dead.retry_count = transition->retry_count;
            dead.last_error = error_summary;
            dead.last_failed_handler = handler_name;
            dead.last_failed_at = failed_at;
            dead.locked_at = {};
            dead.claim_token.clear();
            dead.locked_by.clear();
            ++summary.dead_lettered;
            audit_dead_letter(dead, summary);
        } else {
            ++summary.failed;
        }
    }

    return summary;
}

std::chrono::seconds OutboxPublisher::retry_delay(int retry_count) const {
    static constexpr std::array<std::chrono::seconds, 5> kDelays{
        std::chrono::minutes(1),
        std::chrono::minutes(5),
        std::chrono::minutes(15),
        std::chrono::hours(1),
        std::chrono::hours(6)};
    const auto index = static_cast<std::size_t>(
        std::clamp(retry_count, 1, static_cast<int>(kDelays.size())) - 1);
    return kDelays[index];
}

void OutboxPublisher::audit_dead_letter(
    const OutboxMessage& message,
    OutboxPublishSummary& summary) {
    if (dead_letter_handler_ == nullptr) {
        return;
    }
    try {
        auto handled = dead_letter_handler_->handle_dead_letter(message);
        if (handled) {
            ++summary.dead_letters_audited;
        } else {
            ++summary.audit_failures;
        }
    } catch (...) {
        ++summary.audit_failures;
    }
}

domain::RepositoryVoidResult OutboxPublisher::audit_unhandled_dead_letters(
    OutboxPublishSummary& summary) {
    if (dead_letter_handler_ == nullptr ||
        config_.dead_letter_audit_batch_size == 0) {
        return {};
    }
    auto pending = outbox_.list_unhandled_dead_letters(
        dead_letter_handler_->dead_letter_handler_name(),
        config_.dead_letter_audit_batch_size);
    if (!pending) {
        return std::unexpected(pending.error());
    }
    for (const auto& message : *pending) {
        audit_dead_letter(message, summary);
    }
    return {};
}

} // namespace pfh::application
