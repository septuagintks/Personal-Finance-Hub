// Personal Finance Hub - PostgreSQL Authenticated Request Scope

#pragma once

#include "pfh/application/persistence/i_request_scope.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/account_repository_impl.h"
#include "pfh/infrastructure/persistence/audit_log_repository_impl.h"
#include "pfh/infrastructure/persistence/category_repository_impl.h"
#include "pfh/infrastructure/persistence/drogon_unit_of_work.h"
#include "pfh/infrastructure/persistence/exchange_rate_repository_impl.h"
#include "pfh/infrastructure/persistence/idempotency_repository_impl.h"
#include "pfh/infrastructure/persistence/tag_repository_impl.h"
#include "pfh/infrastructure/persistence/transaction_repository_impl.h"
#include "pfh/infrastructure/persistence/user_preference_repository_impl.h"

#include <drogon/orm/DbClient.h>

#include <memory>
#include <utility>

namespace pfh::infrastructure {

class PostgresRequestScope final : public application::IRequestScope {
public:
    PostgresRequestScope(drogon::orm::DbClientPtr db, domain::UserId user_id)
        : user_id_(user_id),
          accounts_(db, user_id),
          transactions_(db, user_id),
          categories_(db, user_id),
          tags_(db, user_id),
          preferences_(db, user_id),
          exchange_rates_(db),
          idempotency_(user_id),
          uow_(std::move(db), user_id) {}

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
    AccountRepositoryImpl accounts_;
    TransactionRepositoryImpl transactions_;
    CategoryRepositoryImpl categories_;
    TagRepositoryImpl tags_;
    UserPreferenceRepositoryImpl preferences_;
    ExchangeRateRepositoryImpl exchange_rates_;
    AuditLogRepositoryImpl audit_logs_;
    IdempotencyRepositoryImpl idempotency_;
    DrogonUnitOfWork uow_;
};

class PostgresRequestScopeFactory final : public application::IRequestScopeFactory {
public:
    explicit PostgresRequestScopeFactory(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] std::unique_ptr<application::IRequestScope> create(
        domain::UserId user_id) override {
        return std::make_unique<PostgresRequestScope>(db_, user_id);
    }

private:
    drogon::orm::DbClientPtr db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
