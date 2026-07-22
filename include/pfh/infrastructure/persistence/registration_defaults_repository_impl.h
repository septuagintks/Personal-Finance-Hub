// Personal Finance Hub - PostgreSQL Registration Defaults Repository

#pragma once

#include "pfh/application/ports/i_registration_defaults_repository.h"

#ifdef PFH_HAS_POSTGRESQL

namespace pfh::infrastructure {

class RegistrationDefaultsRepositoryImpl final
    : public application::IRegistrationDefaultsRepository {
public:
    [[nodiscard]] domain::RepositoryResult<application::RegistrationDefaultsResult>
    initialize(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        const domain::Currency& base_currency,
        std::string_view preferred_locale,
        std::optional<std::string_view> preferred_timezone) override;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
