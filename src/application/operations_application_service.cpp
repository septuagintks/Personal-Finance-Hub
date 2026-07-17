// Personal Finance Hub - Operations Application Service

#include "pfh/application/services/operations_application_service.h"

#include "pfh/application/error_mapping.h"
#include "pfh/application/idempotency.h"

#include <algorithm>
#include <string>
#include <utility>

namespace pfh::application {

VoidResult OperationsApplicationService::require_operator(
    const AccessTokenClaims& claims) {
    if (!claims.user_id.is_valid() || claims.role != domain::UserRole::Operator) {
        return err(Error::forbidden("Operator role is required"));
    }
    return ok();
}

Result<bool> OperationsApplicationService::readiness() {
    auto state = repository_.readiness(expected_migration_version_);
    if (!state) return err(from_repository(state.error()));
    bool jobs_ready = true;
    if (scheduler_required_) {
        jobs_ready = !jobs_.empty() && std::ranges::all_of(
            jobs_, [](const auto* job) {
                return job != nullptr &&
                       job->runtime_snapshot().scheduler_started;
            });
    }
    return state->database_ready && state->migrations_current && jobs_ready;
}

Result<OperationalOverview> OperationsApplicationService::overview(
    const AccessTokenClaims& claims) {
    if (auto authorized = require_operator(claims); !authorized) {
        return err(authorized.error());
    }
    const auto now = clock_.now();
    auto data = repository_.summary(now);
    if (!data) return err(from_repository(data.error()));
    OperationalOverview result;
    result.data = std::move(*data);
    result.generated_at = now;
    result.jobs.reserve(jobs_.size());
    for (const auto* job : jobs_) {
        if (job != nullptr) result.jobs.push_back(job->runtime_snapshot());
    }
    return result;
}

Result<DeadLetterPage> OperationsApplicationService::list_dead_letters(
    const AccessTokenClaims& claims,
    std::optional<std::string_view> cursor,
    std::size_t limit) {
    if (auto authorized = require_operator(claims); !authorized) {
        return err(authorized.error());
    }
    if (limit == 0 || limit > 100 ||
        (cursor.has_value() && (cursor->empty() || cursor->size() > 128))) {
        return err(Error::validation("Dead-letter query is invalid"));
    }
    auto page = repository_.list_dead_letters(cursor, limit);
    return page ? Result<DeadLetterPage>(std::move(*page))
                : err(from_repository(page.error()));
}

Result<RetryDeadLetterResult> OperationsApplicationService::retry_dead_letter(
    const AccessTokenClaims& claims,
    std::string outbox_id,
    std::string idempotency_key,
    std::string trace_id) {
    if (auto authorized = require_operator(claims); !authorized) {
        return err(authorized.error());
    }
    if (outbox_id.empty() || outbox_id.size() > 64 || trace_id.empty() ||
        trace_id.size() > 128) {
        return err(Error::validation("Dead-letter retry request is invalid"));
    }
    if (auto valid = validate_idempotency_key(idempotency_key); !valid) {
        return err(valid.error());
    }
    auto result = repository_.retry_dead_letter(RetryDeadLetterCommand{
        claims.user_id,
        std::move(outbox_id),
        std::move(idempotency_key),
        std::move(trace_id),
        clock_.now()});
    return result ? Result<RetryDeadLetterResult>(std::move(*result))
                  : err(from_repository(result.error()));
}

} // namespace pfh::application
