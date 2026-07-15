// Personal Finance Hub - In-Memory Authenticated Request Scope

#pragma once

#include "pfh/application/persistence/i_request_scope.h"
#include "pfh/infrastructure/persistence/in_memory_account_repository.h"
#include "pfh/infrastructure/persistence/in_memory_audit_log_repository.h"
#include "pfh/infrastructure/persistence/in_memory_category_repository.h"
#include "pfh/infrastructure/persistence/in_memory_exchange_rate_repository.h"
#include "pfh/infrastructure/persistence/in_memory_idempotency_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include "pfh/infrastructure/persistence/in_memory_tag_repository.h"
#include "pfh/infrastructure/persistence/in_memory_transaction_repository.h"
#include "pfh/infrastructure/persistence/in_memory_unit_of_work.h"
#include "pfh/infrastructure/persistence/in_memory_user_repository.h"

#include <memory>

namespace pfh::infrastructure {

class InMemoryRequestScope final : public application::IRequestScope {
public:
    InMemoryRequestScope(InMemoryStore& store, domain::UserId user_id)
        : user_id_(user_id),
          transactions_(store),
          accounts_(store, transactions_),
          categories_(store),
          tags_(store),
          preferences_(store),
          exchange_rates_(store),
          audit_logs_(store),
          idempotency_(store),
          uow_(store, user_id) {}

    [[nodiscard]] domain::UserId user_id() const noexcept override { return user_id_; }
    [[nodiscard]] domain::IAccountRepository& accounts() noexcept override {
        return accounts_;
    }
    [[nodiscard]] domain::ITransactionRepository& transactions() noexcept override {
        return transactions_;
    }
    [[nodiscard]] domain::ICategoryRepository& categories() noexcept override {
        return categories_;
    }
    [[nodiscard]] domain::ITagRepository& tags() noexcept override { return tags_; }
    [[nodiscard]] domain::IUserPreferenceRepository& preferences() noexcept override {
        return preferences_;
    }
    [[nodiscard]] domain::IExchangeRateRepository& exchange_rates() noexcept override {
        return exchange_rates_;
    }
    [[nodiscard]] domain::IAuditLogRepository& audit_logs() noexcept override {
        return audit_logs_;
    }
    [[nodiscard]] application::IIdempotencyRepository& idempotency() noexcept override {
        return idempotency_;
    }
    [[nodiscard]] application::IUnitOfWork& unit_of_work() noexcept override {
        return uow_;
    }

private:
    domain::UserId user_id_;
    InMemoryTransactionRepository transactions_;
    InMemoryAccountRepository accounts_;
    InMemoryCategoryRepository categories_;
    InMemoryTagRepository tags_;
    InMemoryUserPreferenceRepository preferences_;
    InMemoryExchangeRateRepository exchange_rates_;
    InMemoryAuditLogRepository audit_logs_;
    InMemoryIdempotencyRepository idempotency_;
    InMemoryUnitOfWork uow_;
};

class InMemoryRequestScopeFactory final : public application::IRequestScopeFactory {
public:
    explicit InMemoryRequestScopeFactory(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] std::unique_ptr<application::IRequestScope> create(
        domain::UserId user_id) override {
        return std::make_unique<InMemoryRequestScope>(store_, user_id);
    }

private:
    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
