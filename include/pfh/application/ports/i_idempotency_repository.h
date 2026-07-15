// Personal Finance Hub - Request Idempotency Port

#pragma once

#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/user.h"

#include <chrono>
#include <map>
#include <string>
#include <string_view>

namespace pfh::application {

struct IdempotencyBeginResult {
    bool replay = false;
    std::map<std::string, std::string> response_values;
};

class IIdempotencyRepository {
public:
    virtual ~IIdempotencyRepository() = default;

    [[nodiscard]] virtual domain::RepositoryResult<IdempotencyBeginResult> begin(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        std::string_view operation,
        std::string_view key,
        std::string_view request_fingerprint,
        std::chrono::system_clock::time_point created_at,
        std::chrono::system_clock::time_point expires_at) = 0;

    [[nodiscard]] virtual domain::RepositoryVoidResult complete(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        std::string_view operation,
        std::string_view key,
        const std::map<std::string, std::string>& response_values) = 0;
};

} // namespace pfh::application
