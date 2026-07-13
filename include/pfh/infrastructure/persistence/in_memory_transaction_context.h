// Personal Finance Hub - In-Memory Transaction Context
// Version: 1.0
// C++23
//
// Opaque transaction handle for in-memory persistence tests.
// Real Drogon implementation will wrap drogon::orm::Transaction.

#pragma once

#include "pfh/application/persistence/i_bootstrap_unit_of_work.h"
#include <cstdint>
#include <optional>

namespace pfh::infrastructure {

class InMemoryTransactionContext final
    : public application::ITenantBootstrapTransaction {
public:
    explicit InMemoryTransactionContext(
        std::uint64_t id,
        std::optional<domain::UserId> tenant_user_id = std::nullopt)
        : id_(id), tenant_user_id_(tenant_user_id) {}

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

    [[nodiscard]] domain::RepositoryVoidResult bind_tenant_once(
        domain::UserId user_id) override {
        if (!user_id.is_valid()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Cannot bind an invalid tenant user id"));
        }
        if (tenant_user_id_.has_value()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Transaction tenant is already bound"));
        }
        tenant_user_id_ = user_id;
        return {};
    }

    [[nodiscard]] std::optional<domain::UserId> tenant_user_id() const noexcept override {
        return tenant_user_id_;
    }

private:
    std::uint64_t id_;
    std::optional<domain::UserId> tenant_user_id_;
};

} // namespace pfh::infrastructure
