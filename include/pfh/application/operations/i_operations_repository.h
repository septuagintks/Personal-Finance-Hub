// Personal Finance Hub - Operational Persistence Port

#pragma once

#include "pfh/application/operations/operational_models.h"
#include "pfh/domain/repositories/repository_error.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace pfh::application {

class IOperationsRepository {
public:
    virtual ~IOperationsRepository() = default;

    [[nodiscard]] virtual domain::RepositoryResult<ReadinessState> readiness(
        std::int64_t expected_migration_version) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<OperationalDataSummary>
    summary(std::chrono::system_clock::time_point now) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<DeadLetterPage>
    list_dead_letters(
        std::optional<std::string_view> cursor,
        std::size_t limit) = 0;

    [[nodiscard]] virtual domain::RepositoryResult<RetryDeadLetterResult>
    retry_dead_letter(const RetryDeadLetterCommand& command) = 0;
};

class IJobRuntimeStatusReader {
public:
    virtual ~IJobRuntimeStatusReader() = default;
    [[nodiscard]] virtual JobRuntimeSnapshot runtime_snapshot() const = 0;
};

} // namespace pfh::application
