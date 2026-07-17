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
#include <chrono>
#include <string>
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

    /// @brief Historical read that also returns soft-deleted categories.
    /// Transaction creation must use the active-only method above.
    [[nodiscard]] virtual RepositoryResult<Category>
    find_by_id_for_user_including_deleted(
        CategoryId id,
        UserId user_id) = 0;

    /// @brief Load an owned category in the active write transaction and lock
    /// it against concurrent soft deletion or board changes.
    [[nodiscard]] virtual RepositoryResult<Category> find_by_id_for_user_for_update(
        ITransactionContext& tx,
        CategoryId id,
        UserId user_id) = 0;

    /// @brief Lock an owned category regardless of soft-deletion state.
    [[nodiscard]] virtual RepositoryResult<Category>
    find_by_id_for_user_including_deleted_for_update(
        ITransactionContext& tx,
        CategoryId id,
        UserId user_id) = 0;

    /// @brief Lock the identity used by create-or-restore. System categories
    /// match by template id; user categories match by board, parent and name.
    [[nodiscard]] virtual RepositoryResult<Category> find_identity_for_update(
        ITransactionContext& tx,
        UserId user_id,
        CategoryBoard board,
        const std::optional<CategoryId>& parent_id,
        const std::string& name,
        const std::optional<std::int64_t>& template_id) = 0;

    /// @brief All non-deleted categories on a board for the user.
    [[nodiscard]] virtual RepositoryResult<std::vector<Category>> find_by_board(
        UserId user_id,
        CategoryBoard board) = 0;

    /// @brief All non-deleted categories for the user (both boards).
    [[nodiscard]] virtual RepositoryResult<std::vector<Category>> find_all_for_user(
        UserId user_id) = 0;

    /// @brief All categories for reporting, including soft-deleted history.
    [[nodiscard]] virtual RepositoryResult<std::vector<Category>>
    find_all_for_user_including_deleted(UserId user_id) = 0;

    /// @brief Resolve the first-level (root) category id that `id` rolls up to.
    /// If `id` is already a root, returns it unchanged. Used by reporting to
    /// aggregate historical spend under top-level categories. Soft-deleted
    /// nodes remain resolvable so old transactions retain their category name.
    /// Enforces ownership; a broken parent chain surfaces as a DatabaseError.
    [[nodiscard]] virtual RepositoryResult<CategoryId> resolve_root_id_for_user(
        CategoryId id,
        UserId user_id) = 0;

    /// @brief Read a global system template. The requested locale is preferred;
    /// implementations may fall back to zh-CN only when the same template id
    /// exists in that locale.
    [[nodiscard]] virtual RepositoryResult<SystemCategoryTemplate> find_template_by_id(
        std::int64_t template_id,
        const std::string& locale) = 0;

    /// @brief Insert or update a category (create when id is invalid).
    [[nodiscard]] virtual RepositoryResult<CategoryId> save(
        ITransactionContext& tx,
        const Category& category) = 0;

    [[nodiscard]] virtual RepositoryVoidResult soft_delete(
        ITransactionContext& tx,
        CategoryId id,
        UserId user_id,
        std::chrono::system_clock::time_point deleted_at) = 0;
};

} // namespace pfh::domain
