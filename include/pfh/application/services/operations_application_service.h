// Personal Finance Hub - Operations Application Service

#pragma once

#include "pfh/application/error.h"
#include "pfh/application/operations/i_operations_repository.h"
#include "pfh/application/ports/i_clock.h"
#include "pfh/application/security/auth_models.h"

#include <cstdint>
#include <vector>

namespace pfh::application {

class OperationsApplicationService {
public:
    OperationsApplicationService(
        IOperationsRepository& repository,
        const IClock& clock,
        std::vector<const IJobRuntimeStatusReader*> jobs,
        bool scheduler_required,
        std::int64_t expected_migration_version)
        : repository_(repository),
          clock_(clock),
          jobs_(std::move(jobs)),
          scheduler_required_(scheduler_required),
          expected_migration_version_(expected_migration_version) {}

    [[nodiscard]] Result<bool> readiness();
    [[nodiscard]] Result<OperationalOverview> overview(
        const AccessTokenClaims& claims);
    [[nodiscard]] Result<DeadLetterPage> list_dead_letters(
        const AccessTokenClaims& claims,
        std::optional<std::string_view> cursor,
        std::size_t limit);
    [[nodiscard]] Result<RetryDeadLetterResult> retry_dead_letter(
        const AccessTokenClaims& claims,
        std::string outbox_id,
        std::string idempotency_key,
        std::string trace_id);

private:
    [[nodiscard]] static VoidResult require_operator(
        const AccessTokenClaims& claims);

    IOperationsRepository& repository_;
    const IClock& clock_;
    std::vector<const IJobRuntimeStatusReader*> jobs_;
    bool scheduler_required_ = false;
    std::int64_t expected_migration_version_ = 0;
};

} // namespace pfh::application
