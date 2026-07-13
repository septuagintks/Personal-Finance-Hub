// Personal Finance Hub - PostgreSQL Category Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_category_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for ICategoryRepository.
///
/// `categories` IS RLS-scoped. Reads on pooled connections need the GUC set.
class CategoryRepositoryImpl final : public domain::ICategoryRepository {
public:
    explicit CategoryRepositoryImpl(drogon::orm::DbClientPtr db)
        : db_(std::move(db)) {}

    [[nodiscard]] domain::RepositoryResult<domain::Category> find_by_id_for_user(
        domain::CategoryId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Category> find_by_id_for_user_for_update(
        domain::ITransactionContext& tx,
        domain::CategoryId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Category>> find_by_board(
        domain::UserId user_id,
        domain::CategoryBoard board) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Category>> find_all_for_user(
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::CategoryId> resolve_root_id_for_user(
        domain::CategoryId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::CategoryId> save(
        domain::ITransactionContext& tx,
        const domain::Category& category) override;

private:
    drogon::orm::DbClientPtr db_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL