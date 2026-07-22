// Personal Finance Hub - Registration Defaults Repository Port

#pragma once

#include "pfh/application/security/auth_models.h"
#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"

#include <string_view>
#include <optional>

namespace pfh::application {

class IRegistrationDefaultsRepository {
public:
    virtual ~IRegistrationDefaultsRepository() = default;

    [[nodiscard]] virtual domain::RepositoryResult<RegistrationDefaultsResult>
    initialize(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        const domain::Currency& base_currency,
        std::string_view preferred_locale,
        std::optional<std::string_view> preferred_timezone) = 0;
};

} // namespace pfh::application
