// Personal Finance Hub - Registration Bootstrap Transaction Boundary

#pragma once

#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/domain/user.h"

#include <functional>
#include <optional>

namespace pfh::application {

class ITenantBootstrapTransaction : public domain::ITransactionContext {
public:
    ~ITenantBootstrapTransaction() override = default;

    [[nodiscard]] virtual domain::RepositoryVoidResult bind_tenant_once(
        domain::UserId user_id) = 0;
    [[nodiscard]] virtual std::optional<domain::UserId> tenant_user_id() const noexcept = 0;
};

class IBootstrapUnitOfWork : public IUnitOfWork {
public:
    ~IBootstrapUnitOfWork() override = default;

    [[nodiscard]] virtual domain::RepositoryVoidResult execute_bootstrap_transaction(
        std::function<domain::RepositoryVoidResult(ITenantBootstrapTransaction& tx)>
            action) = 0;
};

} // namespace pfh::application
