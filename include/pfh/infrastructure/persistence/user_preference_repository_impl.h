// Personal Finance Hub - PostgreSQL UserPreference Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_user_preference_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>
#include <utility>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for IUserPreferenceRepository.
///
/// `user_preferences` IS RLS-scoped (V1 §RLS). Repository reads run with the
/// request's `app.current_user_id` set on the transaction/connection. The
/// fallback path (no row yet) needs `users.base_currency_code`, which is NOT
/// RLS-scoped — the request is already authenticated, so reading the user's
/// own row from `users` is acceptable.
class UserPreferenceRepositoryImpl final : public domain::IUserPreferenceRepository {
public:
    UserPreferenceRepositoryImpl(
        drogon::orm::DbClientPtr db,
        domain::UserId tenant_user_id)
        : db_(std::move(db)), tenant_user_id_(tenant_user_id) {}

    [[nodiscard]] domain::RepositoryResult<domain::UserPreference> find_by_user(
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::UserPreference> find_by_user(
        domain::ITransactionContext& tx,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryVoidResult save(
        domain::ITransactionContext& tx,
        const domain::UserPreference& preference) override;

private:
    drogon::orm::DbClientPtr db_;
    domain::UserId tenant_user_id_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
