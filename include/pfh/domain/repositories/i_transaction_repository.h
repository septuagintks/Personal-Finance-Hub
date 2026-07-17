// Personal Finance Hub - Transaction Repository Interface
// Version: 1.0
// C++23

#pragma once

#include "pfh/domain/repositories/i_transaction_context.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/tag.h"
#include "pfh/domain/transaction.h"
#include "pfh/domain/transfer_aggregate.h"
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pfh::domain {

/// @brief Identifiers assigned by save_transfer for a persisted transfer.
///
/// The repository assigns the group id and both leg ids as part of the same
/// write, so callers get them back directly instead of re-querying inside the
/// transaction (a non-transactional read would not see the uncommitted rows on
/// a real PostgreSQL connection).
struct TransferPersistResult {
    TransferGroupId group_id;
    TransactionId outgoing_id;
    TransactionId incoming_id;
};

/// @brief Persisted transfer aggregate read model. Amounts in transactions use
/// storage signs; Application maps them back to REST magnitudes.
struct TransferSnapshot {
    TransferGroupId group_id;
    UserId user_id;
    int transfer_mode = 0;
    std::optional<Decimal> exchange_rate;
    std::vector<Transaction> transactions;
};

/// @brief Stable keyset used by the ledger read path. The public cursor is an
/// opaque Application concern; repositories receive the decoded values only.
struct TransactionPageCursor {
    std::chrono::system_clock::time_point occurred_at{};
    TransactionId id;
};

/// @brief Tenant-scoped ledger filters. `occurred_from` is inclusive and
/// `occurred_to` is exclusive. Results never include soft-deleted rows.
struct TransactionPageQuery {
    UserId user_id;
    std::optional<AccountId> account_id;
    std::optional<TransactionType> type;
    std::optional<CategoryId> category_id;
    std::optional<TagId> tag_id;
    std::optional<std::chrono::system_clock::time_point> occurred_from;
    std::optional<std::chrono::system_clock::time_point> occurred_to;
    std::string keyword;
    std::optional<TransactionPageCursor> before;
    std::size_t limit = 50;
};

/// @brief Ledger projection preserving historical metadata and correction
/// links. A deleted category/tag remains visible on an old transaction.
struct TransactionReadModel {
    Transaction transaction;
    std::optional<std::string> category_name;
    bool category_deleted = false;
    std::vector<Tag> tags;
    std::optional<TransactionId> corrects_transaction_id;
    std::optional<TransactionId> corrected_by_transaction_id;
};

struct TransactionPageResult {
    std::vector<TransactionReadModel> items;
    bool has_more = false;
};

struct TransactionCorrectionPersistResult {
    Transaction replacement;
};

class ITransactionRepository {
public:
    virtual ~ITransactionRepository() = default;

    [[nodiscard]] virtual RepositoryResult<Transaction> find_by_id(TransactionId id) = 0;

    [[nodiscard]] virtual RepositoryResult<TransactionReadModel> find_detail(
        TransactionId id,
        UserId user_id,
        bool include_deleted = true) = 0;

    [[nodiscard]] virtual RepositoryResult<TransactionPageResult> find_page(
        const TransactionPageQuery& query) = 0;

    /// @brief Load a transaction inside the active transaction and take a row
    /// lock (PostgreSQL `SELECT ... FOR UPDATE`). Write paths that check state
    /// (e.g. not-already-deleted) before mutating must use this so the check and
    /// the write are serialized against concurrent deletes. Requires an active
    /// transaction context.
    [[nodiscard]] virtual RepositoryResult<Transaction> find_by_id_for_update(
        ITransactionContext& tx,
        TransactionId id) = 0;

    /// @brief Persist a single (non-transfer) transaction and return the fully
    /// persisted entity (with its assigned id and normalized storage sign).
    /// Callers must not re-read after commit: on RLS-scoped connections a
    /// post-commit read on another connection may not see the row, and a read
    /// failure after a successful commit would wrongly surface as an API error
    /// (risking duplicate re-submits).
    [[nodiscard]] virtual RepositoryResult<Transaction> save_single(
        ITransactionContext& tx,
        const Transaction& transaction) = 0;

    /// @brief Persist a TransferAggregate atomically:
    /// transfer_groups row + outgoing + incoming (+ adjustments).
    /// Returns the assigned group id together with both leg ids so callers do
    /// not have to re-read within the same transaction.
    [[nodiscard]] virtual RepositoryResult<TransferPersistResult> save_transfer(
        ITransactionContext& tx,
        const TransferAggregate& transfer) = 0;

    /// @brief Find account transactions in the half-open range [from, to).
    [[nodiscard]] virtual RepositoryResult<std::vector<Transaction>> find_by_account(
        AccountId account_id,
        std::optional<std::chrono::system_clock::time_point> from = std::nullopt,
        std::optional<std::chrono::system_clock::time_point> to = std::nullopt,
        bool include_deleted = false) = 0;

    [[nodiscard]] virtual RepositoryResult<std::vector<Transaction>> find_by_user(
        UserId user_id,
        bool include_deleted = false) = 0;

    /// @brief Find user transactions in the half-open range [from, to), ordered
    /// by occurred_at and then id so report consumers can process one pass.
    [[nodiscard]] virtual RepositoryResult<std::vector<Transaction>>
    find_by_user_in_range(
        UserId user_id,
        std::optional<std::chrono::system_clock::time_point> from,
        std::optional<std::chrono::system_clock::time_point> to,
        bool include_deleted = false) = 0;

    [[nodiscard]] virtual RepositoryResult<TransferSnapshot> find_transfer_by_group(
        TransferGroupId group_id,
        UserId user_id) = 0;

    [[nodiscard]] virtual RepositoryVoidResult soft_delete(
        ITransactionContext& tx,
        TransactionId id,
        UserId user_id,
        std::chrono::system_clock::time_point deleted_at) = 0;

    /// @brief Atomically persist an append-only correction: create the
    /// replacement, soft-delete the active original, link both facts and
    /// invalidate every affected balance cache. The caller must already hold
    /// the original transaction lock inside the supplied context.
    [[nodiscard]] virtual RepositoryResult<TransactionCorrectionPersistResult>
    save_correction(
        ITransactionContext& tx,
        TransactionId original_id,
        const Transaction& replacement,
        std::chrono::system_clock::time_point corrected_at) = 0;

    /// @brief Physically delete this account's NON-transfer transactions only.
    /// Associated transaction_tag_relations are deleted first. Transfer legs
    /// are handled by physical_delete_transfers_touching_account
    /// so the paired leg on the other account and the transfer_groups row are
    /// removed atomically as a unit (never leaving an orphan leg).
    [[nodiscard]] virtual RepositoryVoidResult physical_delete_by_account(
        ITransactionContext& tx,
        AccountId account_id) = 0;

    /// @brief Physically delete every transfer aggregate that touches this
    /// account through either a Transfer leg or grouped Adjustment: both legs,
    /// every grouped Adjustment, their tag relations, and the transfer_groups
    /// row. This prevents dangerous account deletion from leaving a partial
    /// aggregate or failing after Tag support is enabled.
    [[nodiscard]] virtual RepositoryVoidResult physical_delete_transfers_touching_account(
        ITransactionContext& tx,
        AccountId account_id) = 0;
};

} // namespace pfh::domain
