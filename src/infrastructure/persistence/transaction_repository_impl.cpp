// Personal Finance Hub - PostgreSQL Transaction Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/transaction_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/transfer_aggregate.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

namespace pfh::infrastructure {

namespace {

domain::RepositoryResult<domain::Transaction> map_transaction_row(
    const drogon::orm::Row& row) {
    try {
        const auto id = domain::TransactionId(pg::getBigInt(row, 0));
        const auto user_id = domain::UserId(pg::getBigInt(row, 1));
        const auto account_id = domain::AccountId(pg::getBigInt(row, 2));
        const auto category_id = pg::getOptionalBigInt(row, 3);
        const auto type = pg::parseTransactionType(pg::getString(row, 4));
        const auto amount_str = pg::getNumericAsString(row, 5);
        const auto currency_code = pg::getString(row, 6);
        const auto description = pg::getOptionalString(row, 7).value_or("");
        const auto transfer_group_id = pg::getOptionalBigInt(row, 8);
        const auto deleted_at = pg::getOptionalTimestamp(row, 9);
        const auto occurred_at = pg::getTimestamp(row, 10);
        const auto created_at = pg::getTimestamp(row, 11);

        auto amount_dec = domain::Decimal::parse_numeric_20_8(amount_str);
        if (!amount_dec.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Invalid amount: " + amount_str));
        }
        auto currency = domain::Currency::create(currency_code);
        if (!currency.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Unknown currency: " + currency_code));
        }
        domain::Money amount(*amount_dec, *currency);

        std::optional<domain::CategoryId> cat_opt;
        if (category_id.has_value()) {
            cat_opt = domain::CategoryId(*category_id);
        }
        std::optional<domain::TransferGroupId> tg_opt;
        if (transfer_group_id.has_value()) {
            tg_opt = domain::TransferGroupId(*transfer_group_id);
        }

        return domain::Transaction(
            id, user_id, account_id, amount, type, occurred_at,
            description, cat_opt, tg_opt, created_at, deleted_at);
    } catch (const std::exception& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("map_transaction_row: ") + e.what()));
    }
}

constexpr const char* kSelectColumns = R"SQL(
    id, user_id, account_id, category_id, type::text, amount::text,
    currency_code, description, transfer_group_id, deleted_at,
    transaction_time, created_at
)SQL";

}  // namespace

domain::RepositoryResult<domain::Transaction>
TransactionRepositoryImpl::find_by_id(domain::TransactionId id) {
    const std::string sql =
        std::string("SELECT ") + kSelectColumns +
        " FROM transactions WHERE id = $1";
    try {
        auto result = db_->execSqlSync(sql, id.value);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found: " + id.to_string()));
        }
        return map_transaction_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_id: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::Transaction>
TransactionRepositoryImpl::find_by_id_for_update(
    domain::ITransactionContext& tx_iface,
    domain::TransactionId id) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    const std::string sql =
        std::string("SELECT ") + kSelectColumns +
        " FROM transactions WHERE id = $1 FOR UPDATE";
    try {
        auto result = tx.execSqlSync(sql, id.value);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found: " + id.to_string()));
        }
        return map_transaction_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_id_for_update: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::Transaction>
TransactionRepositoryImpl::save_single(
    domain::ITransactionContext& tx_iface,
    const domain::Transaction& transaction) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    // Transfer legs MUST go through save_transfer (aggregate write).
    if (transaction.type() == domain::TransactionType::Transfer) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transfer transactions must be written via save_transfer"));
    }

    // Normalize storage sign: domain constructs positive magnitudes for Expense;
    // repository negates them before INSERT.
    domain::Money storage_amount = transaction.amount();
    if (transaction.type() == domain::TransactionType::Expense &&
        storage_amount.is_positive()) {
        storage_amount = storage_amount.negated();
    } else if (transaction.type() == domain::TransactionType::Income &&
               storage_amount.is_negative()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Income amount must not be negative"));
    }

    constexpr const char* kInsertSql = R"SQL(
        INSERT INTO transactions (
            user_id, account_id, category_id, type, amount, currency_code,
            description, transfer_group_id, transaction_time, created_at)
        VALUES (
            $1, $2, $3, $4::transaction_type, $5::numeric(20,8), $6,
            $7, $8, $9, $10)
        RETURNING id
    )SQL";

    try {
        std::optional<std::int64_t> cat_val;
        if (transaction.category_id().has_value()) {
            cat_val = transaction.category_id()->value;
        }
        std::optional<std::int64_t> tg_val;
        if (transaction.transfer_group_id().has_value()) {
            tg_val = transaction.transfer_group_id()->value;
        }

        auto result = tx.execSqlSync(
            kInsertSql,
            transaction.user_id().value,
            transaction.account_id().value,
            cat_val,
            pg::toSqlText(transaction.type()),
            storage_amount.amount().to_string(),
            storage_amount.currency().code(),
            transaction.description(),
            tg_val,
            transaction.occurred_at(),
            transaction.created_at());

        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "insert transaction: no id returned"));
        }
        const auto id = domain::TransactionId(pg::getBigInt(result[0], 0));

        // Return the persisted entity with its assigned id and storage sign.
        return domain::Transaction(
            id,
            transaction.user_id(),
            transaction.account_id(),
            storage_amount,
            transaction.type(),
            transaction.occurred_at(),
            transaction.description(),
            transaction.category_id(),
            transaction.transfer_group_id(),
            transaction.created_at(),
            transaction.deleted_at());
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("save_single: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::TransferPersistResult>
TransactionRepositoryImpl::save_transfer(
    domain::ITransactionContext& tx_iface,
    const domain::TransferAggregate& transfer) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    const auto& outgoing = transfer.outgoing();
    const auto& incoming = transfer.incoming();

    // Validation: both sides must be Transfer type and share the same user.
    if (outgoing.type() != domain::TransactionType::Transfer ||
        incoming.type() != domain::TransactionType::Transfer) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transfer sides must be TransactionType::Transfer"));
    }
    if (outgoing.user_id() != incoming.user_id()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transfer sides must belong to the same user"));
    }

    // Insert transfer_groups row first to obtain the group id.
    constexpr const char* kInsertGroupSql = R"SQL(
        INSERT INTO transfer_groups (
            user_id, transfer_mode, exchange_rate, exchange_rate_provider,
            exchange_rate_snapshot_time, created_at)
        VALUES ($1, $2, $3::numeric(20,10), $4, $5, $6)
        RETURNING id
    )SQL";

    domain::TransferGroupId group_id;
    try {
        std::optional<std::string> rate_str;
        std::optional<std::string> rate_source;
        std::optional<std::chrono::system_clock::time_point> rate_time;
        if (transfer.rate().has_value()) {
            rate_str = transfer.rate()->rate().to_string();
            rate_source = transfer.rate()->source();
            rate_time = transfer.rate()->fetched_at();
        }

        auto result = tx.execSqlSync(
            kInsertGroupSql,
            outgoing.user_id().value,
            static_cast<int>(transfer.mode()),
            rate_str,
            rate_source,
            rate_time,
            outgoing.created_at());
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "insert transfer_groups: no id returned"));
        }
        group_id = domain::TransferGroupId(pg::getBigInt(result[0], 0));
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("insert transfer_groups: ") + e.base().what()));
    }

    // Insert outgoing and incoming legs with signed amount convention:
    // outgoing is negative, incoming is positive.
    constexpr const char* kInsertTxSql = R"SQL(
        INSERT INTO transactions (
            user_id, account_id, type, amount, currency_code, description,
            transfer_group_id, transaction_time, created_at)
        VALUES (
            $1, $2, $3::transaction_type, $4::numeric(20,8), $5,
            $6, $7, $8, $9)
        RETURNING id
    )SQL";

    domain::TransactionId outgoing_id;
    domain::TransactionId incoming_id;

    try {
        // Outgoing: negate the domain's positive magnitude.
        const auto out_amount = outgoing.amount().negated();
        auto out_result = tx.execSqlSync(
            kInsertTxSql,
            outgoing.user_id().value,
            outgoing.account_id().value,
            pg::toSqlText(domain::TransactionType::Transfer),
            out_amount.amount().to_string(),
            out_amount.currency().code(),
            outgoing.description(),
            group_id.value,
            outgoing.occurred_at(),
            outgoing.created_at());
        if (out_result.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "insert outgoing: no id returned"));
        }
        outgoing_id = domain::TransactionId(pg::getBigInt(out_result[0], 0));

        // Incoming: keep positive as provided.
        const auto in_amount = incoming.amount();
        auto in_result = tx.execSqlSync(
            kInsertTxSql,
            incoming.user_id().value,
            incoming.account_id().value,
            pg::toSqlText(domain::TransactionType::Transfer),
            in_amount.amount().to_string(),
            in_amount.currency().code(),
            incoming.description(),
            group_id.value,
            incoming.occurred_at(),
            incoming.created_at());
        if (in_result.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "insert incoming: no id returned"));
        }
        incoming_id = domain::TransactionId(pg::getBigInt(in_result[0], 0));

        // Persist adjustments as independent transactions (save_single rejects
        // Transfer type, but Adjustment is accepted).
        for (const auto& adj : transfer.adjustments()) {
            auto adj_result = save_single(tx_iface, adj);
            if (!adj_result.has_value()) {
                return std::unexpected(adj_result.error());
            }
        }

        return domain::TransferPersistResult{group_id, outgoing_id, incoming_id};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("save_transfer legs: ") + e.base().what()));
    }
}

domain::RepositoryResult<std::vector<domain::Transaction>>
TransactionRepositoryImpl::find_by_account(
    domain::AccountId account_id,
    std::optional<std::chrono::system_clock::time_point> from,
    std::optional<std::chrono::system_clock::time_point> to,
    bool include_deleted) {
    std::string sql = std::string("SELECT ") + kSelectColumns +
                      " FROM transactions WHERE account_id = $1";
    if (!include_deleted) {
        sql += " AND deleted_at IS NULL";
    }
    if (from.has_value()) {
        sql += " AND transaction_time >= $2";
    }
    if (to.has_value()) {
        sql += from.has_value() ? " AND transaction_time <= $3"
                                : " AND transaction_time <= $2";
    }
    sql += " ORDER BY transaction_time, id";

    try {
        drogon::orm::Result result;
        if (from.has_value() && to.has_value()) {
            result = db_->execSqlSync(sql, account_id.value, *from, *to);
        } else if (from.has_value()) {
            result = db_->execSqlSync(sql, account_id.value, *from);
        } else if (to.has_value()) {
            result = db_->execSqlSync(sql, account_id.value, *to);
        } else {
            result = db_->execSqlSync(sql, account_id.value);
        }

        std::vector<domain::Transaction> txs;
        txs.reserve(result.size());
        for (const auto& row : result) {
            auto tx = map_transaction_row(row);
            if (!tx.has_value()) {
                return std::unexpected(tx.error());
            }
            txs.push_back(std::move(*tx));
        }
        return txs;
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_account: ") + e.base().what()));
    }
}

domain::RepositoryResult<std::vector<domain::Transaction>>
TransactionRepositoryImpl::find_by_user(
    domain::UserId user_id,
    bool include_deleted) {
    std::string sql = std::string("SELECT ") + kSelectColumns +
                      " FROM transactions WHERE user_id = $1";
    if (!include_deleted) {
        sql += " AND deleted_at IS NULL";
    }
    sql += " ORDER BY transaction_time, id";

    try {
        auto result = db_->execSqlSync(sql, user_id.value);
        std::vector<domain::Transaction> txs;
        txs.reserve(result.size());
        for (const auto& row : result) {
            auto tx = map_transaction_row(row);
            if (!tx.has_value()) {
                return std::unexpected(tx.error());
            }
            txs.push_back(std::move(*tx));
        }
        return txs;
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_user: ") + e.base().what()));
    }
}

domain::RepositoryVoidResult TransactionRepositoryImpl::soft_delete(
    domain::ITransactionContext& tx_iface,
    domain::TransactionId id,
    domain::UserId user_id,
    std::chrono::system_clock::time_point deleted_at) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    constexpr const char* kSql = R"SQL(
        UPDATE transactions SET deleted_at = $1
        WHERE id = $2 AND user_id = $3 AND deleted_at IS NULL
    )SQL";
    try {
        auto result = tx.execSqlSync(kSql, deleted_at, id.value, user_id.value);
        if (result.affectedRows() == 0) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found for user or already deleted"));
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("soft_delete: ") + e.base().what()));
    }
}

domain::RepositoryVoidResult TransactionRepositoryImpl::physical_delete_by_account(
    domain::ITransactionContext& tx_iface,
    domain::AccountId account_id) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    constexpr const char* kSql = R"SQL(
        DELETE FROM transactions
        WHERE account_id = $1 AND type != 'transfer'::transaction_type
    )SQL";
    try {
        tx.execSqlSync(kSql, account_id.value);
        return {};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("physical_delete_by_account: ") + e.base().what()));
    }
}

domain::RepositoryVoidResult
TransactionRepositoryImpl::physical_delete_transfers_touching_account(
    domain::ITransactionContext& tx_iface,
    domain::AccountId account_id) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    // Delete all transfer_groups whose id appears in any Transfer transaction
    // that touches this account, then delete those transactions.
    constexpr const char* kDeleteGroupsSql = R"SQL(
        DELETE FROM transfer_groups
        WHERE id IN (
            SELECT DISTINCT transfer_group_id
            FROM transactions
            WHERE account_id = $1 AND type = 'transfer'::transaction_type
        )
    )SQL";
    constexpr const char* kDeleteTxsSql = R"SQL(
        DELETE FROM transactions
        WHERE transfer_group_id IN (
            SELECT DISTINCT transfer_group_id
            FROM transactions
            WHERE account_id = $1 AND type = 'transfer'::transaction_type
        )
    )SQL";
    try {
        tx.execSqlSync(kDeleteTxsSql, account_id.value);
        tx.execSqlSync(kDeleteGroupsSql, account_id.value);
        return {};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("physical_delete_transfers_touching_account: ") +
            e.base().what()));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
