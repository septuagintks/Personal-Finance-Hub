// Personal Finance Hub - PostgreSQL Request Idempotency Repository

#pragma once

#include "pfh/application/ports/i_idempotency_repository.h"

#ifdef PFH_HAS_POSTGRESQL

namespace pfh::infrastructure {

class IdempotencyRepositoryImpl final
    : public application::IIdempotencyRepository {
public:
    explicit IdempotencyRepositoryImpl(domain::UserId tenant_user_id)
        : tenant_user_id_(tenant_user_id) {}

    [[nodiscard]] domain::RepositoryResult<application::IdempotencyBeginResult>
    begin(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        std::string_view operation,
        std::string_view key,
        std::string_view request_fingerprint,
        std::chrono::system_clock::time_point created_at,
        std::chrono::system_clock::time_point expires_at) override;

    [[nodiscard]] domain::RepositoryVoidResult complete(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        std::string_view operation,
        std::string_view key,
        const std::map<std::string, std::string>& response_values) override;

private:
    domain::UserId tenant_user_id_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
