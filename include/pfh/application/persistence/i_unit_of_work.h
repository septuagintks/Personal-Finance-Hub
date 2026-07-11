// Personal Finance Hub - Unit of Work Interface
// Version: 1.0
// C++23
//
// Application-layer transaction boundary. Implementations must:
// 1. Share one DB transaction across all repository writes in the closure.
// 2. Write outbox rows in the same transaction as business facts.
// 3. Commit only after both succeed; rollback discards both.
// 4. Never publish events before commit.

#pragma once

#include "pfh/domain/events/i_domain_event.h"
#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include <functional>
#include <memory>

namespace pfh::application {

class IUnitOfWork {
public:
    virtual ~IUnitOfWork() = default;

    /// @brief Register a domain event to be written to outbox on commit.
    virtual void register_event(std::shared_ptr<domain::IDomainEvent> event) = 0;

    /// @brief Execute action inside a single transaction context.
    /// On success: write pending outbox events, then commit.
    /// On failure: rollback business writes and discard pending events.
    [[nodiscard]] virtual domain::RepositoryVoidResult execute_in_transaction(
        std::function<domain::RepositoryVoidResult(domain::ITransactionContext& tx)> action) = 0;
};

} // namespace pfh::application
