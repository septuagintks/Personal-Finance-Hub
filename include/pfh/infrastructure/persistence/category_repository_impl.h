// Personal Finance Hub - PostgreSQL Category Repository
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_category_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>
#include <utility>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter for ICategoryRepository.
///
/// Instances are request-scoped; all reads and writes are bound to one tenant.
class CategoryRepositoryImpl final : public domain::ICategoryRepository {
public:
    CategoryRepositoryImpl(
        drogon::orm::DbClientPtr db,
        domain::UserId tenant_user_id)
        : db_(std::move(db)), tenant_user_id_(tenant_user_id) {}

    [[nodiscard]] domain::RepositoryResult<domain::Category> find_by_id_for_user(
        domain::CategoryId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Category>
    find_by_id_for_user_including_deleted(
        domain::CategoryId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Category> find_by_id_for_user_for_update(
        domain::ITransactionContext& tx,
        domain::CategoryId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Category>
    find_by_id_for_user_including_deleted_for_update(
        domain::ITransactionContext& tx,
        domain::CategoryId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Category> find_identity_for_update(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        domain::CategoryBoard board,
        const std::optional<domain::CategoryId>& parent_id,
        const std::string& name,
        const std::optional<std::int64_t>& template_id) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Category>> find_by_board(
        domain::UserId user_id,
        domain::CategoryBoard board) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Category>> find_all_for_user(
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Category>>
    find_all_for_user_including_deleted(domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::CategoryId> resolve_root_id_for_user(
        domain::CategoryId id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::SystemCategoryTemplate>
    find_template_by_id(
        std::int64_t template_id,
        const std::string& locale) override;

    [[nodiscard]] domain::RepositoryResult<domain::CategoryId> save(
        domain::ITransactionContext& tx,
        const domain::Category& category) override;

    [[nodiscard]] domain::RepositoryVoidResult soft_delete(
        domain::ITransactionContext& tx,
        domain::CategoryId id,
        domain::UserId user_id,
        std::chrono::system_clock::time_point deleted_at) override;

private:
    drogon::orm::DbClientPtr db_;
    domain::UserId tenant_user_id_;
};

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
