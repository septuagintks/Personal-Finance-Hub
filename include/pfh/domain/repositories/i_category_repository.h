// Personal Finance Hub - Category Repository Interface
// Version: 1.0
// C++23
//
// Resolves category persistence so use cases and query services no longer
// depend on callers passing a CategoryBoard explicitly (tasks #47). All reads
// are user-scoped for multi-tenant isolation.

#pragma once

#include "pfh/domain/category.h"
#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include <vector>

namespace pfh::domain {

class ICategoryRepository {
public:
    virtual ~ICategoryRepository() = default;

    /// @brief Find a category by id, enforcing ownership (user isolation).
    /// Returns NotFound for a foreign or absent category (no existence leak).
    [[nodiscard]] virtual RepositoryResult<Category> find_by_id_for_user(
        CategoryId id,
        UserId user_id) = 0;

    /// @brief All non-deleted categories on a board for the user.
    [[nodiscard]] virtual RepositoryResult<std::vector<Category>> find_by_board(
        UserId user_id,
        CategoryBoard board) = 0;

    /// @brief All non-deleted categories for the user (both boards).
    [[nodiscard]] virtual RepositoryResult<std::vector<Category>> find_all_for_user(
        UserId user_id) = 0;

    /// @brief Resolve the first-level (root) category id that `id` rolls up to.
    /// If `id` is already a root, returns it unchanged. Used by reporting to
    /// aggregate spend under top-level categories. Enforces ownership; a broken
    /// parent chain surfaces as a DatabaseError rather than an infinite walk.
    [[nodiscard]] virtual RepositoryResult<CategoryId> resolve_root_id_for_user(
        CategoryId id,
        UserId user_id) = 0;

    /// @brief Insert or update a category (create when id is invalid).
    [[nodiscard]] virtual RepositoryResult<CategoryId> save(
        ITransactionContext& tx,
        const Category& category) = 0;
};

} // namespace pfh::domain
