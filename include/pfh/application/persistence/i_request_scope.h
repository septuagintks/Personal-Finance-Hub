// Personal Finance Hub - Authenticated Application Request Scope

#pragma once

#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/application/ports/i_idempotency_repository.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"
#include "pfh/domain/repositories/i_category_repository.h"
#include "pfh/domain/repositories/i_exchange_rate_repository.h"
#include "pfh/domain/repositories/i_tag_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/domain/repositories/i_user_preference_repository.h"

#include <memory>

namespace pfh::application {

class ICashFlowProjection;

class IRequestScope {
public:
    virtual ~IRequestScope() = default;

    [[nodiscard]] virtual domain::UserId user_id() const noexcept = 0;
    [[nodiscard]] virtual domain::IAccountRepository& accounts() noexcept = 0;
    [[nodiscard]] virtual domain::ITransactionRepository& transactions() noexcept = 0;
    [[nodiscard]] virtual domain::ICategoryRepository& categories() noexcept = 0;
    [[nodiscard]] virtual domain::ITagRepository& tags() noexcept = 0;
    [[nodiscard]] virtual domain::IUserPreferenceRepository& preferences() noexcept = 0;
    [[nodiscard]] virtual domain::IExchangeRateRepository& exchange_rates() noexcept = 0;
    [[nodiscard]] virtual domain::IAuditLogRepository& audit_logs() noexcept = 0;
    [[nodiscard]] virtual IIdempotencyRepository& idempotency() noexcept = 0;
    [[nodiscard]] virtual IUnitOfWork& unit_of_work() noexcept = 0;
    [[nodiscard]] virtual ICashFlowProjection* cash_flow_projection() noexcept {
        return nullptr;
    }
};

class IRequestScopeFactory {
public:
    virtual ~IRequestScopeFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<IRequestScope> create(
        domain::UserId user_id) = 0;
};

} // namespace pfh::application
