// Personal Finance Hub - PostgreSQL Operations Adapter

#pragma once

#include "pfh/application/operations/i_operations_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class PostgresOperationsRepository final
    : public application::IOperationsRepository {
public:
    explicit PostgresOperationsRepository(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<application::ReadinessState>
    readiness(std::int64_t expected_migration_version) override;

    [[nodiscard]] domain::RepositoryResult<application::OperationalDataSummary>
    summary(std::chrono::system_clock::time_point now) override;

    [[nodiscard]] domain::RepositoryResult<application::DeadLetterPage>
    list_dead_letters(
        std::optional<std::string_view> cursor,
        std::size_t limit) override;

    [[nodiscard]] domain::RepositoryResult<application::RetryDeadLetterResult>
    retry_dead_letter(
        const application::RetryDeadLetterCommand& command) override;

private:
    drogon::orm::DbClientPtr db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
