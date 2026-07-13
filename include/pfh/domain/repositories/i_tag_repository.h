// Personal Finance Hub - Tag Repository Interface

#pragma once

#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/tag.h"
#include "pfh/domain/transaction.h"

#include <chrono>
#include <vector>

namespace pfh::domain {

class ITagRepository {
public:
    virtual ~ITagRepository() = default;

    [[nodiscard]] virtual RepositoryResult<std::vector<Tag>> find_by_user(
        UserId user_id,
        bool include_deleted = false) = 0;

    [[nodiscard]] virtual RepositoryResult<Tag> find_by_id_for_user(
        TagId tag_id,
        UserId user_id) = 0;

    /// @brief Load an active owned tag in the current write transaction and
    /// lock it so the audit snapshot and soft deletion are one atomic decision.
    [[nodiscard]] virtual RepositoryResult<Tag> find_by_id_for_user_for_update(
        ITransactionContext& tx,
        TagId tag_id,
        UserId user_id) = 0;

    [[nodiscard]] virtual RepositoryResult<std::vector<Tag>> find_by_transaction(
        TransactionId transaction_id,
        UserId user_id) = 0;

    [[nodiscard]] virtual RepositoryResult<TagId> save(
        ITransactionContext& tx,
        const Tag& tag) = 0;

    [[nodiscard]] virtual RepositoryVoidResult soft_delete(
        ITransactionContext& tx,
        TagId tag_id,
        UserId user_id,
        std::chrono::system_clock::time_point deleted_at) = 0;

    /// @brief Atomically replace all tag relations for an owned transaction.
    /// Every supplied tag must be active and owned by the same user.
    [[nodiscard]] virtual RepositoryResult<std::vector<Tag>> replace_transaction_tags(
        ITransactionContext& tx,
        TransactionId transaction_id,
        UserId user_id,
        const std::vector<TagId>& tag_ids) = 0;
};

} // namespace pfh::domain
