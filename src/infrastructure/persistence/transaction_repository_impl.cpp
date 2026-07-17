// Personal Finance Hub - PostgreSQL Transaction Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/transaction_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/category.h"
#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

namespace {

constexpr const char* kTransactionColumns = R"SQL(
    id, user_id, account_id, category_id, type::text, amount::text,
    currency_code, description, transfer_group_id, deleted_at,
    transaction_time, created_at
)SQL";

constexpr const char* kTransactionReadColumns = R"SQL(
    t.id, t.user_id, t.account_id, t.category_id, t.type::text,
    t.amount::text, t.currency_code, t.description, t.transfer_group_id,
    t.deleted_at, t.transaction_time, t.created_at,
    c.name, (c.id IS NOT NULL AND c.deleted_at IS NOT NULL),
    replacement_link.original_transaction_id,
    original_link.replacement_transaction_id
)SQL";

domain::RepositoryError transaction_not_found() {
    return domain::RepositoryError::not_found(
        "Transaction not found for user");
}

domain::RepositoryResult<domain::Transaction> map_transaction_row(
    const drogon::orm::Row& row) {
    try {
        const auto amount = domain::Decimal::parse_numeric_20_8(
            pg::getNumericAsString(row, 5));
        const auto currency = domain::Currency::create(pg::getString(row, 6));
        if (!amount.has_value() || !currency.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Stored transaction amount or currency is invalid"));
        }

        const auto category_value = pg::getOptionalBigInt(row, 3);
        const auto group_value = pg::getOptionalBigInt(row, 8);
        std::optional<domain::CategoryId> category_id;
        std::optional<domain::TransferGroupId> group_id;
        if (category_value.has_value()) {
            category_id = domain::CategoryId(*category_value);
        }
        if (group_value.has_value()) {
            group_id = domain::TransferGroupId(*group_value);
        }

        return domain::Transaction(
            domain::TransactionId(pg::getBigInt(row, 0)),
            domain::UserId(pg::getBigInt(row, 1)),
            domain::AccountId(pg::getBigInt(row, 2)),
            domain::Money(*amount, *currency),
            pg::parseTransactionType(pg::getString(row, 4)),
            pg::getTimestamp(row, 10),
            pg::getOptionalString(row, 7).value_or(""),
            category_id,
            group_id,
            pg::getTimestamp(row, 11),
            pg::getOptionalTimestamp(row, 9));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored transaction row is invalid"));
    }
}

domain::RepositoryResult<domain::Tag> map_transaction_tag_row(
    const drogon::orm::Row& row) {
    try {
        return domain::Tag(
            domain::TagId(pg::getBigInt(row, 1)),
            domain::UserId(pg::getBigInt(row, 2)),
            pg::getString(row, 3),
            pg::getOptionalTimestamp(row, 4),
            pg::getTimestamp(row, 5),
            pg::getTimestamp(row, 6));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored transaction tag row is invalid"));
    }
}

domain::RepositoryResult<domain::TransactionReadModel>
map_transaction_read_row(const drogon::orm::Row& row) {
    auto transaction = map_transaction_row(row);
    if (!transaction) return std::unexpected(transaction.error());
    try {
        domain::TransactionReadModel result{
            *transaction,
            pg::getOptionalString(row, 12),
            pg::getBool(row, 13),
            {},
            std::nullopt,
            std::nullopt};
        if (const auto original = pg::getOptionalBigInt(row, 14);
            original.has_value()) {
            result.corrects_transaction_id = domain::TransactionId(*original);
        }
        if (const auto replacement = pg::getOptionalBigInt(row, 15);
            replacement.has_value()) {
            result.corrected_by_transaction_id =
                domain::TransactionId(*replacement);
        }
        return result;
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored transaction projection is invalid"));
    }
}

domain::RepositoryVoidResult append_transaction_tags(
    drogon::orm::Transaction& database,
    domain::UserId user_id,
    std::vector<domain::TransactionReadModel>& models) {
    if (models.empty()) return {};

    nlohmann::json ids = nlohmann::json::array();
    std::map<std::int64_t, std::size_t> positions;
    for (std::size_t index = 0; index < models.size(); ++index) {
        const auto id = models[index].transaction.id().value();
        ids.push_back(id);
        positions.emplace(id, index);
    }
    constexpr const char* kTagsSql = R"SQL(
        SELECT relation.transaction_id, tag.id, tag.user_id, tag.name,
               tag.deleted_at, tag.created_at, tag.updated_at
        FROM jsonb_array_elements_text($1::jsonb) WITH ORDINALITY
             AS requested(id, position)
        JOIN transaction_tag_relations relation
          ON relation.transaction_id = requested.id::bigint
         AND relation.user_id = $2
        JOIN transaction_tags tag
          ON tag.id = relation.tag_id AND tag.user_id = relation.user_id
        ORDER BY requested.position, tag.name, tag.id
    )SQL";
    const auto rows = database.execSqlSync(kTagsSql, ids.dump(), user_id.value());
    for (const auto& row : rows) {
        const auto position = positions.find(pg::getBigInt(row, 0));
        if (position == positions.end()) {
            return std::unexpected(domain::RepositoryError::database(
                "Transaction tag projection referenced an unknown row"));
        }
        auto tag = map_transaction_tag_row(row);
        if (!tag) return std::unexpected(tag.error());
        models[position->second].tags.push_back(std::move(*tag));
    }
    return {};
}

domain::RepositoryVoidResult validate_amount_for_storage(
    const domain::Money& amount) {
    if (amount.is_zero()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transaction amount must be non-zero"));
    }
    if (!amount.amount().fits_numeric_20_8()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transaction amount does not fit NUMERIC(20,8)"));
    }
    return {};
}

domain::RepositoryVoidResult validate_transaction_references(
    drogon::orm::Transaction& database,
    const domain::Transaction& transaction) {
    constexpr const char* kAccountSql = R"SQL(
        SELECT currency_code
        FROM accounts
        WHERE id = $1 AND user_id = $2
        FOR UPDATE NOWAIT
    )SQL";
    const auto account_result = database.execSqlSync(
        kAccountSql,
        transaction.account_id().value(),
        transaction.user_id().value());
    if (account_result.empty()) {
        return std::unexpected(domain::RepositoryError::not_found(
            "Transaction account not found for user"));
    }
    if (pg::getString(account_result[0], 0) !=
        transaction.amount().currency().code()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transaction currency does not match account currency"));
    }

    if (!transaction.category_id().has_value()) {
        return {};
    }
    constexpr const char* kCategorySql = R"SQL(
        SELECT board::text
        FROM categories
        WHERE id = $1 AND user_id = $2 AND deleted_at IS NULL
    )SQL";
    const auto category_result = database.execSqlSync(
        kCategorySql,
        transaction.category_id()->value(),
        transaction.user_id().value());
    if (category_result.empty()) {
        return std::unexpected(domain::RepositoryError::not_found(
            "Transaction category not found for user"));
    }
    const auto board = pg::parseCategoryBoard(
        pg::getString(category_result[0], 0));
    if (!domain::Category::validate_category_board(transaction.type(), board)
             .has_value()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Category board does not match transaction type"));
    }
    return {};
}

domain::RepositoryResult<domain::Transaction> insert_transaction(
    drogon::orm::Transaction& database,
    const domain::Transaction& source,
    const domain::Money& storage_amount,
    std::optional<domain::TransferGroupId> group_id) {
    constexpr const char* kInsertSql = R"SQL(
        INSERT INTO transactions (
            user_id, account_id, category_id, type, amount, currency_code,
            description, transfer_group_id, transaction_time, created_at)
        VALUES (
            $1, $2, $3, $4::transaction_type, $5::numeric(20,8), $6,
            $7, $8, $9, $10)
        RETURNING id
    )SQL";

    std::optional<std::int64_t> category_value;
    if (source.category_id().has_value()) {
        category_value = source.category_id()->value();
    }
    std::optional<std::int64_t> group_value;
    if (group_id.has_value()) {
        group_value = group_id->value();
    }

    const auto result = database.execSqlSync(
        kInsertSql,
        source.user_id().value(),
        source.account_id().value(),
        category_value,
        pg::toSqlText(source.type()),
        storage_amount.amount().to_string(),
        storage_amount.currency().code(),
        source.description(),
        group_value,
        pg::toDbTimestamp(source.occurred_at()),
        pg::toDbTimestamp(source.created_at()));
    if (result.empty()) {
        return std::unexpected(domain::RepositoryError::database(
            "Transaction insert returned no identifier"));
    }

    return domain::Transaction(
        domain::TransactionId(pg::getBigInt(result[0], 0)),
        source.user_id(),
        source.account_id(),
        storage_amount,
        source.type(),
        source.occurred_at(),
        source.description(),
        source.category_id(),
        group_id,
        source.created_at(),
        source.deleted_at());
}

void invalidate_balance_cache(
    drogon::orm::Transaction& database,
    domain::AccountId account_id,
    domain::UserId user_id) {
    database.execSqlSync(
        "DELETE FROM account_balance_cache "
        "WHERE account_id = $1 AND user_id = $2",
        account_id.value(),
        user_id.value());
}

domain::RepositoryResult<domain::TransferSnapshot> load_transfer_snapshot(
    drogon::orm::Transaction& database,
    const drogon::orm::Row& group_row,
    bool lock_members) {
    try {
        domain::TransferSnapshot snapshot;
        snapshot.group_id = domain::TransferGroupId(pg::getBigInt(group_row, 0));
        snapshot.user_id = domain::UserId(pg::getBigInt(group_row, 1));
        const auto mode = pg::getBigInt(group_row, 2);
        if (mode < 1 || mode > 3) {
            return std::unexpected(domain::RepositoryError::database(
                "Stored transfer mode is invalid"));
        }
        snapshot.transfer_mode = static_cast<int>(mode);
        if (const auto rate_text = pg::getOptionalString(group_row, 3);
            rate_text.has_value()) {
            auto rate = domain::Decimal::parse_numeric_20_10(*rate_text);
            if (!rate) {
                return std::unexpected(domain::RepositoryError::database(
                    "Stored transfer rate is invalid"));
            }
            snapshot.exchange_rate = *rate;
        }
        snapshot.note = pg::getString(group_row, 4);
        snapshot.created_at = pg::getTimestamp(group_row, 5);
        if (const auto original = pg::getOptionalBigInt(group_row, 6);
            original.has_value()) {
            snapshot.corrects_group_id = domain::TransferGroupId(*original);
        }
        if (const auto replacement = pg::getOptionalBigInt(group_row, 7);
            replacement.has_value()) {
            snapshot.corrected_by_group_id =
                domain::TransferGroupId(*replacement);
        }

        std::string sql = std::string("SELECT ") + kTransactionColumns +
            " FROM transactions WHERE transfer_group_id = $1 "
            "AND user_id = $2 ORDER BY account_id, id";
        if (lock_members) sql += " FOR UPDATE NOWAIT";
        const auto rows = database.execSqlSync(
            sql, snapshot.group_id.value(), snapshot.user_id.value());
        if (rows.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "Persisted transfer has no transactions"));
        }
        snapshot.transactions.reserve(rows.size());
        bool any_active = false;
        bool any_deleted = false;
        std::optional<std::chrono::system_clock::time_point> latest_deleted;
        for (const auto& row : rows) {
            auto transaction = map_transaction_row(row);
            if (!transaction) return std::unexpected(transaction.error());
            if (snapshot.occurred_at.time_since_epoch().count() == 0 ||
                transaction->occurred_at() < snapshot.occurred_at) {
                snapshot.occurred_at = transaction->occurred_at();
            }
            if (transaction->is_deleted()) {
                any_deleted = true;
                if (!latest_deleted.has_value() ||
                    *transaction->deleted_at() > *latest_deleted) {
                    latest_deleted = transaction->deleted_at();
                }
            } else {
                any_active = true;
            }
            snapshot.transactions.push_back(std::move(*transaction));
        }
        if (any_active && any_deleted) {
            return std::unexpected(domain::RepositoryError::database(
                "Persisted transfer is partially deleted"));
        }
        if (any_deleted) snapshot.deleted_at = latest_deleted;
        return snapshot;
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored transfer projection is invalid"));
    }
}

constexpr const char* kTransferGroupProjectionSql = R"SQL(
    SELECT tg.id, tg.user_id, tg.transfer_mode, tg.exchange_rate::text,
           COALESCE(tg.note, ''), tg.created_at,
           replacement_link.original_transfer_group_id,
           original_link.replacement_transfer_group_id
    FROM transfer_groups tg
    LEFT JOIN transfer_corrections replacement_link
      ON replacement_link.replacement_transfer_group_id = tg.id
     AND replacement_link.user_id = tg.user_id
    LEFT JOIN transfer_corrections original_link
      ON original_link.original_transfer_group_id = tg.id
     AND original_link.user_id = tg.user_id
)SQL";

}  // namespace

domain::RepositoryResult<domain::Transaction>
TransactionRepositoryImpl::find_by_id(domain::TransactionId id) {
    return postgres::execute_tenant_read<domain::Transaction>(
        db_, tenant_user_id_, "find transaction", [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") +
                kTransactionColumns +
                " FROM transactions WHERE id = $1 AND user_id = $2";
            const auto result = transaction->execSqlSync(
                sql, id.value(), tenant_user_id_.value());
            if (result.empty()) {
                return domain::RepositoryResult<domain::Transaction>(
                    std::unexpected(transaction_not_found()));
            }
            return map_transaction_row(result[0]);
        });
}

domain::RepositoryResult<domain::TransactionReadModel>
TransactionRepositoryImpl::find_detail(
    domain::TransactionId id,
    domain::UserId user_id,
    bool include_deleted) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(transaction_not_found());
    }
    return postgres::execute_tenant_read<domain::TransactionReadModel>(
        db_, tenant_user_id_, "find transaction detail", [&](const auto& transaction) {
            std::string sql = std::string("SELECT ") + kTransactionReadColumns + R"SQL(
                FROM transactions t
                LEFT JOIN categories c
                  ON c.id = t.category_id AND c.user_id = t.user_id
                LEFT JOIN transaction_corrections replacement_link
                  ON replacement_link.replacement_transaction_id = t.id
                 AND replacement_link.user_id = t.user_id
                LEFT JOIN transaction_corrections original_link
                  ON original_link.original_transaction_id = t.id
                 AND original_link.user_id = t.user_id
                WHERE t.id = $1 AND t.user_id = $2
            )SQL";
            if (!include_deleted) sql += " AND t.deleted_at IS NULL";
            const auto rows = transaction->execSqlSync(
                sql, id.value(), user_id.value());
            if (rows.empty()) {
                return domain::RepositoryResult<domain::TransactionReadModel>(
                    std::unexpected(transaction_not_found()));
            }
            auto model = map_transaction_read_row(rows[0]);
            if (!model) return model;
            std::vector<domain::TransactionReadModel> models;
            models.push_back(std::move(*model));
            if (auto tags = append_transaction_tags(
                    *transaction, user_id, models);
                !tags) {
                return domain::RepositoryResult<domain::TransactionReadModel>(
                    std::unexpected(tags.error()));
            }
            return domain::RepositoryResult<domain::TransactionReadModel>(
                std::move(models.front()));
        });
}

domain::RepositoryResult<domain::TransactionPageResult>
TransactionRepositoryImpl::find_page(
    const domain::TransactionPageQuery& query) {
    if (query.user_id != tenant_user_id_ || query.limit == 0 ||
        query.limit > 200) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transaction page query is invalid"));
    }
    return postgres::execute_tenant_read<domain::TransactionPageResult>(
        db_, tenant_user_id_, "list transaction page", [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") +
                kTransactionReadColumns + R"SQL(
                FROM transactions t
                LEFT JOIN categories c
                  ON c.id = t.category_id AND c.user_id = t.user_id
                LEFT JOIN transaction_corrections replacement_link
                  ON replacement_link.replacement_transaction_id = t.id
                 AND replacement_link.user_id = t.user_id
                LEFT JOIN transaction_corrections original_link
                  ON original_link.original_transaction_id = t.id
                 AND original_link.user_id = t.user_id
                WHERE t.user_id = $1 AND t.deleted_at IS NULL
                  AND ($2::bigint IS NULL OR t.account_id = $2)
                  AND ($3::text IS NULL OR t.type = $3::transaction_type)
                  AND ($4::bigint IS NULL OR t.category_id = $4)
                  AND ($5::bigint IS NULL OR EXISTS (
                      SELECT 1 FROM transaction_tag_relations filtered_tag
                      WHERE filtered_tag.transaction_id = t.id
                        AND filtered_tag.user_id = t.user_id
                        AND filtered_tag.tag_id = $5))
                  AND ($6::timestamptz IS NULL OR t.transaction_time >= $6)
                  AND ($7::timestamptz IS NULL OR t.transaction_time < $7)
                  AND ($8::text = '' OR
                       POSITION(lower($8) IN lower(COALESCE(t.description, ''))) > 0)
                  AND ($9::timestamptz IS NULL OR
                       (t.transaction_time, t.id) < ($9, $10::bigint))
                ORDER BY t.transaction_time DESC, t.id DESC
                LIMIT $11
            )SQL";

            std::optional<std::int64_t> account;
            if (query.account_id.has_value()) account = query.account_id->value();
            std::optional<std::string> type;
            if (query.type.has_value()) type = pg::toSqlText(*query.type);
            std::optional<std::int64_t> category;
            if (query.category_id.has_value()) category = query.category_id->value();
            std::optional<std::int64_t> tag;
            if (query.tag_id.has_value()) tag = query.tag_id->value();
            const auto from = pg::toDbTimestamp(query.occurred_from);
            const auto to = pg::toDbTimestamp(query.occurred_to);
            std::optional<trantor::Date> cursor_time;
            std::optional<std::int64_t> cursor_id;
            if (query.before.has_value()) {
                cursor_time = pg::toDbTimestamp(query.before->occurred_at);
                cursor_id = query.before->id.value();
            }
            const auto rows = transaction->execSqlSync(
                sql,
                query.user_id.value(), account, type, category, tag, from, to,
                query.keyword, cursor_time, cursor_id,
                static_cast<std::int64_t>(query.limit + 1U));

            domain::TransactionPageResult page;
            page.has_more = rows.size() > query.limit;
            const auto count = page.has_more ? query.limit : rows.size();
            page.items.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                auto model = map_transaction_read_row(rows[index]);
                if (!model) {
                    return domain::RepositoryResult<domain::TransactionPageResult>(
                        std::unexpected(model.error()));
                }
                page.items.push_back(std::move(*model));
            }
            if (auto tags = append_transaction_tags(
                    *transaction, query.user_id, page.items);
                !tags) {
                return domain::RepositoryResult<domain::TransactionPageResult>(
                    std::unexpected(tags.error()));
            }
            return domain::RepositoryResult<domain::TransactionPageResult>(
                std::move(page));
        });
}

domain::RepositoryResult<domain::Transaction>
TransactionRepositoryImpl::find_by_id_for_update(
    domain::ITransactionContext& tx_iface,
    domain::TransactionId id) {
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    const std::string sql = std::string("SELECT ") + kTransactionColumns +
        " FROM transactions WHERE id = $1 AND user_id = $2 FOR UPDATE NOWAIT";
    try {
        const auto result = (*context)->transaction().execSqlSync(
            sql, id.value(), tenant_user_id_.value());
        if (result.empty()) {
            return std::unexpected(transaction_not_found());
        }
        return map_transaction_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("lock transaction", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("lock transaction", error));
    }
}

domain::RepositoryResult<domain::Transaction>
TransactionRepositoryImpl::save_single(
    domain::ITransactionContext& tx_iface,
    const domain::Transaction& transaction) {
    if (transaction.user_id() != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transaction owner does not match repository tenant"));
    }
    if (transaction.id().is_valid() || transaction.is_deleted()) {
        return std::unexpected(domain::RepositoryError::validation(
            "save_single only accepts new active transactions"));
    }
    if (transaction.transfer_group_id().has_value()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Grouped transactions must be written via save_transfer"));
    }
    if (transaction.type() != domain::TransactionType::Income &&
        transaction.type() != domain::TransactionType::Expense &&
        transaction.type() != domain::TransactionType::Transfer &&
        transaction.type() != domain::TransactionType::Adjustment) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transaction type is invalid"));
    }
    if (transaction.type() == domain::TransactionType::Transfer) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transfer transactions must be written via save_transfer"));
    }
    if (auto valid = validate_amount_for_storage(transaction.amount()); !valid) {
        return std::unexpected(valid.error());
    }

    domain::Money storage_amount = transaction.amount();
    if (transaction.type() == domain::TransactionType::Expense) {
        if (!storage_amount.is_positive()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Expense amount must be a positive magnitude"));
        }
        storage_amount = storage_amount.negated();
    } else if (transaction.type() == domain::TransactionType::Income &&
               !storage_amount.is_positive()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Income amount must be a positive magnitude"));
    }

    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    try {
        if (auto references = validate_transaction_references(
                (*context)->transaction(), transaction);
            !references.has_value()) {
            return std::unexpected(references.error());
        }
        auto persisted = insert_transaction(
            (*context)->transaction(), transaction, storage_amount, std::nullopt);
        if (!persisted.has_value()) {
            return std::unexpected(persisted.error());
        }
        invalidate_balance_cache(
            (*context)->transaction(),
            transaction.account_id(),
            tenant_user_id_);
        return persisted;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("save transaction", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("save transaction", error));
    }
}

domain::RepositoryResult<domain::TransferPersistResult>
TransactionRepositoryImpl::save_transfer(
    domain::ITransactionContext& tx_iface,
    const domain::TransferAggregate& transfer) {
    const auto& outgoing = transfer.outgoing();
    const auto& incoming = transfer.incoming();
    if (outgoing.user_id() != tenant_user_id_ ||
        incoming.user_id() != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transfer owner does not match repository tenant"));
    }
    if (outgoing.id().is_valid() || incoming.id().is_valid() ||
        outgoing.is_deleted() || incoming.is_deleted()) {
        return std::unexpected(domain::RepositoryError::validation(
            "save_transfer only accepts new active transactions"));
    }
    if ((outgoing.transfer_group_id().has_value() &&
         outgoing.transfer_group_id()->is_valid()) ||
        (incoming.transfer_group_id().has_value() &&
         incoming.transfer_group_id()->is_valid())) {
        return std::unexpected(domain::RepositoryError::validation(
            "New transfer must not carry a persisted group identifier"));
    }
    if (outgoing.type() != domain::TransactionType::Transfer ||
        incoming.type() != domain::TransactionType::Transfer ||
        outgoing.account_id() == incoming.account_id() ||
        !outgoing.amount().is_positive() ||
        !incoming.amount().is_positive()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transfer aggregate invariants are invalid"));
    }
    if (auto valid = validate_amount_for_storage(outgoing.amount()); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = validate_amount_for_storage(incoming.amount()); !valid) {
        return std::unexpected(valid.error());
    }
    if (transfer.rate().has_value() &&
        !transfer.rate()->rate().fits_numeric_20_10()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transfer rate does not fit NUMERIC(20,10)"));
    }
    for (const auto& adjustment : transfer.adjustments()) {
        if (adjustment.user_id() != tenant_user_id_ ||
            adjustment.type() != domain::TransactionType::Adjustment ||
            adjustment.id().is_valid() || adjustment.is_deleted() ||
            adjustment.occurred_at() != outgoing.occurred_at() ||
            adjustment.transfer_group_id() != outgoing.transfer_group_id()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer adjustment invariants are invalid"));
        }
        if (auto valid = validate_amount_for_storage(adjustment.amount()); !valid) {
            return std::unexpected(valid.error());
        }
    }

    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    auto& database = (*context)->transaction();

    try {
        if (auto references = validate_transaction_references(database, outgoing);
            !references.has_value()) {
            return std::unexpected(references.error());
        }
        if (auto references = validate_transaction_references(database, incoming);
            !references.has_value()) {
            return std::unexpected(references.error());
        }
        for (const auto& adjustment : transfer.adjustments()) {
            if (auto references = validate_transaction_references(
                    database, adjustment);
                !references.has_value()) {
                return std::unexpected(references.error());
            }
        }

        constexpr const char* kInsertGroupSql = R"SQL(
            INSERT INTO transfer_groups (
                user_id, note, transfer_mode, exchange_rate,
                exchange_rate_provider, exchange_rate_snapshot_time, created_at)
            VALUES ($1, $2, $3, $4::numeric(20,10), $5, $6, $7)
            RETURNING id
        )SQL";
        std::optional<std::string> rate_value;
        std::optional<std::string> rate_source;
        std::optional<trantor::Date> rate_time;
        if (transfer.rate().has_value()) {
            rate_value = transfer.rate()->rate().to_string();
            rate_source = transfer.rate()->source();
            rate_time = pg::toDbTimestamp(transfer.rate()->fetched_at());
        }

        const auto group_result = database.execSqlSync(
            kInsertGroupSql,
            tenant_user_id_.value(),
            outgoing.description(),
            static_cast<std::int16_t>(transfer.mode()),
            rate_value,
            rate_source,
            rate_time,
            pg::toDbTimestamp(outgoing.created_at()));
        if (group_result.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "Transfer group insert returned no identifier"));
        }
        const domain::TransferGroupId group_id(
            pg::getBigInt(group_result[0], 0));

        auto persisted_outgoing = insert_transaction(
            database, outgoing, outgoing.amount().negated(), group_id);
        if (!persisted_outgoing.has_value()) {
            return std::unexpected(persisted_outgoing.error());
        }
        auto persisted_incoming = insert_transaction(
            database, incoming, incoming.amount(), group_id);
        if (!persisted_incoming.has_value()) {
            return std::unexpected(persisted_incoming.error());
        }

        std::vector<domain::TransactionId> adjustment_ids;
        adjustment_ids.reserve(transfer.adjustments().size());
        for (const auto& adjustment : transfer.adjustments()) {
            auto persisted_adjustment = insert_transaction(
                database, adjustment, adjustment.amount(), group_id);
            if (!persisted_adjustment.has_value()) {
                return std::unexpected(persisted_adjustment.error());
            }
            adjustment_ids.push_back(persisted_adjustment->id());
        }

        invalidate_balance_cache(
            database, outgoing.account_id(), tenant_user_id_);
        invalidate_balance_cache(
            database, incoming.account_id(), tenant_user_id_);
        for (const auto& adjustment : transfer.adjustments()) {
            invalidate_balance_cache(
                database, adjustment.account_id(), tenant_user_id_);
        }

        return domain::TransferPersistResult{
            group_id,
            persisted_outgoing->id(),
            persisted_incoming->id(),
            std::move(adjustment_ids)};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error("save transfer", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("save transfer", error));
    }
}

domain::RepositoryResult<std::vector<domain::Transaction>>
TransactionRepositoryImpl::find_by_account(
    domain::AccountId account_id,
    std::optional<std::chrono::system_clock::time_point> from,
    std::optional<std::chrono::system_clock::time_point> to,
    bool include_deleted) {
    return postgres::execute_tenant_read<std::vector<domain::Transaction>>(
        db_, tenant_user_id_, "list account transactions", [&](const auto& transaction) {
            std::string sql = std::string("SELECT ") + kTransactionColumns +
                " FROM transactions WHERE account_id = $1 AND user_id = $2";
            if (!include_deleted) {
                sql += " AND deleted_at IS NULL";
            }
            if (from.has_value()) {
                sql += " AND transaction_time >= $3";
            }
            if (to.has_value()) {
                sql += from.has_value() ? " AND transaction_time < $4"
                                        : " AND transaction_time < $3";
            }
            sql += " ORDER BY transaction_time, id";

            const auto map_rows = [](const drogon::orm::Result& result) {
                std::vector<domain::Transaction> transactions;
                transactions.reserve(result.size());
                for (const auto& row : result) {
                    auto mapped = map_transaction_row(row);
                    if (!mapped.has_value()) {
                        return domain::RepositoryResult<
                            std::vector<domain::Transaction>>(
                                std::unexpected(mapped.error()));
                    }
                    transactions.push_back(std::move(*mapped));
                }
                return domain::RepositoryResult<std::vector<domain::Transaction>>(
                    std::move(transactions));
            };

            if (from.has_value() && to.has_value()) {
                return map_rows(transaction->execSqlSync(
                    sql,
                    account_id.value(),
                    tenant_user_id_.value(),
                    pg::toDbTimestamp(*from),
                    pg::toDbTimestamp(*to)));
            }
            if (from.has_value()) {
                return map_rows(transaction->execSqlSync(
                    sql,
                    account_id.value(),
                    tenant_user_id_.value(),
                    pg::toDbTimestamp(*from)));
            }
            if (to.has_value()) {
                return map_rows(transaction->execSqlSync(
                    sql,
                    account_id.value(),
                    tenant_user_id_.value(),
                    pg::toDbTimestamp(*to)));
            }
            return map_rows(transaction->execSqlSync(
                sql, account_id.value(), tenant_user_id_.value()));
        });
}

domain::RepositoryResult<std::vector<domain::Transaction>>
TransactionRepositoryImpl::find_by_user(
    domain::UserId user_id,
    bool include_deleted) {
    return find_by_user_in_range(
        user_id, std::nullopt, std::nullopt, include_deleted);
}

domain::RepositoryResult<std::vector<domain::Transaction>>
TransactionRepositoryImpl::find_by_user_in_range(
    domain::UserId user_id,
    std::optional<std::chrono::system_clock::time_point> from,
    std::optional<std::chrono::system_clock::time_point> to,
    bool include_deleted) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(transaction_not_found());
    }
    return postgres::execute_tenant_read<std::vector<domain::Transaction>>(
        db_, tenant_user_id_, "list user transactions", [&](const auto& transaction) {
            std::string sql = std::string("SELECT ") + kTransactionColumns +
                " FROM transactions WHERE user_id = $1";
            if (!include_deleted) {
                sql += " AND deleted_at IS NULL";
            }
            if (from.has_value()) {
                sql += " AND transaction_time >= $2";
            }
            if (to.has_value()) {
                sql += from.has_value() ? " AND transaction_time < $3"
                                        : " AND transaction_time < $2";
            }
            sql += " ORDER BY transaction_time, id";

            const auto map_rows = [](const drogon::orm::Result& result) {
                std::vector<domain::Transaction> transactions;
                transactions.reserve(result.size());
                for (const auto& row : result) {
                    auto mapped = map_transaction_row(row);
                    if (!mapped.has_value()) {
                        return domain::RepositoryResult<
                            std::vector<domain::Transaction>>(
                                std::unexpected(mapped.error()));
                    }
                    transactions.push_back(std::move(*mapped));
                }
                return domain::RepositoryResult<std::vector<domain::Transaction>>(
                    std::move(transactions));
            };
            if (from.has_value() && to.has_value()) {
                return map_rows(transaction->execSqlSync(
                    sql, user_id.value(), pg::toDbTimestamp(*from),
                    pg::toDbTimestamp(*to)));
            }
            if (from.has_value()) {
                return map_rows(transaction->execSqlSync(
                    sql, user_id.value(), pg::toDbTimestamp(*from)));
            }
            if (to.has_value()) {
                return map_rows(transaction->execSqlSync(
                    sql, user_id.value(), pg::toDbTimestamp(*to)));
            }
            return map_rows(transaction->execSqlSync(sql, user_id.value()));
        });
}

domain::RepositoryResult<domain::TransferSnapshot>
TransactionRepositoryImpl::find_transfer_by_group(
    domain::TransferGroupId group_id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::not_found(
            "Transfer not found for user"));
    }
    return postgres::execute_tenant_read<domain::TransferSnapshot>(
        db_, tenant_user_id_, "find transfer", [&](const auto& transaction) {
            const std::string sql = std::string(kTransferGroupProjectionSql) +
                " WHERE tg.id = $1 AND tg.user_id = $2";
            const auto group = transaction->execSqlSync(
                sql, group_id.value(), user_id.value());
            if (group.empty()) {
                return domain::RepositoryResult<domain::TransferSnapshot>(
                    std::unexpected(domain::RepositoryError::not_found(
                        "Transfer not found for user")));
            }
            return load_transfer_snapshot(*transaction, group[0], false);
        });
}

domain::RepositoryResult<domain::TransferPageResult>
TransactionRepositoryImpl::find_transfer_page(
    const domain::TransferPageQuery& query) {
    if (query.user_id != tenant_user_id_ || query.limit == 0 ||
        query.limit > 200) {
        return std::unexpected(domain::RepositoryError::validation(
            "Transfer page query is invalid"));
    }
    return postgres::execute_tenant_read<domain::TransferPageResult>(
        db_, tenant_user_id_, "list transfer page", [&](const auto& transaction) {
            const std::string sql = std::string(kTransferGroupProjectionSql) + R"SQL(
                JOIN LATERAL (
                    SELECT MIN(member.transaction_time) AS occurred_at,
                           BOOL_AND(member.deleted_at IS NULL) AS is_active
                    FROM transactions member
                    WHERE member.transfer_group_id = tg.id
                      AND member.user_id = tg.user_id
                ) aggregate_state ON TRUE
                WHERE tg.user_id = $1
                  AND aggregate_state.is_active
                  AND ($2::bigint IS NULL OR EXISTS (
                      SELECT 1 FROM transactions account_member
                      WHERE account_member.transfer_group_id = tg.id
                        AND account_member.user_id = tg.user_id
                        AND account_member.deleted_at IS NULL
                        AND account_member.account_id = $2))
                  AND ($3::timestamptz IS NULL OR
                       aggregate_state.occurred_at >= $3)
                  AND ($4::timestamptz IS NULL OR
                       aggregate_state.occurred_at < $4)
                  AND ($5::timestamptz IS NULL OR
                       (aggregate_state.occurred_at, tg.id) < ($5, $6::bigint))
                ORDER BY aggregate_state.occurred_at DESC, tg.id DESC
                LIMIT $7
            )SQL";
            std::optional<std::int64_t> account_id;
            if (query.account_id.has_value()) {
                account_id = query.account_id->value();
            }
            std::optional<trantor::Date> cursor_time;
            std::optional<std::int64_t> cursor_id;
            if (query.before.has_value()) {
                cursor_time = pg::toDbTimestamp(query.before->occurred_at);
                cursor_id = query.before->group_id.value();
            }
            const auto rows = transaction->execSqlSync(
                sql,
                query.user_id.value(),
                account_id,
                pg::toDbTimestamp(query.occurred_from),
                pg::toDbTimestamp(query.occurred_to),
                cursor_time,
                cursor_id,
                static_cast<std::int64_t>(query.limit + 1U));
            domain::TransferPageResult page;
            page.has_more = rows.size() > query.limit;
            const auto count = page.has_more ? query.limit : rows.size();
            page.items.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                auto snapshot = load_transfer_snapshot(
                    *transaction, rows[index], false);
                if (!snapshot) {
                    return domain::RepositoryResult<domain::TransferPageResult>(
                        std::unexpected(snapshot.error()));
                }
                page.items.push_back(std::move(*snapshot));
            }
            return domain::RepositoryResult<domain::TransferPageResult>(
                std::move(page));
        });
}

domain::RepositoryResult<domain::TransferSnapshot>
TransactionRepositoryImpl::find_transfer_by_group_for_update(
    domain::ITransactionContext& tx_iface,
    domain::TransferGroupId group_id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::not_found(
            "Transfer not found for user"));
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) return std::unexpected(context.error());
    try {
        const std::string sql = std::string(kTransferGroupProjectionSql) +
            " WHERE tg.id = $1 AND tg.user_id = $2 FOR UPDATE OF tg NOWAIT";
        const auto group = (*context)->transaction().execSqlSync(
            sql, group_id.value(), user_id.value());
        if (group.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transfer not found for user"));
        }
        return load_transfer_snapshot(
            (*context)->transaction(), group[0], true);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("lock transfer aggregate", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("lock transfer aggregate", error));
    }
}

domain::RepositoryVoidResult TransactionRepositoryImpl::soft_delete_transfer(
    domain::ITransactionContext& tx_iface,
    domain::TransferGroupId group_id,
    domain::UserId user_id,
    std::chrono::system_clock::time_point deleted_at) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::not_found(
            "Transfer not found for user"));
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) return std::unexpected(context.error());
    try {
        constexpr const char* kStateSql = R"SQL(
            SELECT id, deleted_at
            FROM transactions
            WHERE transfer_group_id = $1 AND user_id = $2
            ORDER BY account_id, id
            FOR UPDATE NOWAIT
        )SQL";
        const auto state = (*context)->transaction().execSqlSync(
            kStateSql, group_id.value(), user_id.value());
        if (state.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transfer not found for user"));
        }
        for (const auto& row : state) {
            if (pg::getOptionalTimestamp(row, 1).has_value()) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Transfer is already deleted"));
            }
        }
        constexpr const char* kDeleteSql = R"SQL(
            UPDATE transactions
            SET deleted_at = $1, updated_at = $1, version = version + 1
            WHERE transfer_group_id = $2 AND user_id = $3
              AND deleted_at IS NULL
            RETURNING account_id
        )SQL";
        const auto changed = (*context)->transaction().execSqlSync(
            kDeleteSql,
            pg::toDbTimestamp(deleted_at),
            group_id.value(),
            user_id.value());
        if (changed.size() != state.size()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Transfer changed while being deleted"));
        }
        std::set<std::int64_t> accounts;
        for (const auto& row : changed) accounts.insert(pg::getBigInt(row, 0));
        for (const auto account_id : accounts) {
            invalidate_balance_cache(
                (*context)->transaction(),
                domain::AccountId(account_id),
                tenant_user_id_);
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("soft-delete transfer", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("soft-delete transfer", error));
    }
}

domain::RepositoryResult<domain::TransferCorrectionPersistResult>
TransactionRepositoryImpl::save_transfer_correction(
    domain::ITransactionContext& tx_iface,
    domain::TransferGroupId original_group_id,
    const domain::TransferAggregate& replacement,
    std::chrono::system_clock::time_point corrected_at) {
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) return std::unexpected(context.error());
    try {
        const auto existing = (*context)->transaction().execSqlSync(
            "SELECT 1 FROM transfer_corrections "
            "WHERE original_transfer_group_id = $1 AND user_id = $2",
            original_group_id.value(), tenant_user_id_.value());
        if (!existing.empty()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Transfer is already corrected"));
        }
        auto persisted = save_transfer(tx_iface, replacement);
        if (!persisted) return std::unexpected(persisted.error());
        auto deleted = soft_delete_transfer(
            tx_iface, original_group_id, tenant_user_id_, corrected_at);
        if (!deleted) return std::unexpected(deleted.error());
        constexpr const char* kLinkSql = R"SQL(
            INSERT INTO transfer_corrections (
                original_transfer_group_id, replacement_transfer_group_id,
                user_id, corrected_at)
            VALUES ($1, $2, $3, $4)
        )SQL";
        (*context)->transaction().execSqlSync(
            kLinkSql,
            original_group_id.value(),
            persisted->group_id.value(),
            tenant_user_id_.value(),
            pg::toDbTimestamp(corrected_at));
        return domain::TransferCorrectionPersistResult{std::move(*persisted)};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("correct transfer", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("correct transfer", error));
    }
}

domain::RepositoryVoidResult TransactionRepositoryImpl::soft_delete(
    domain::ITransactionContext& tx_iface,
    domain::TransactionId id,
    domain::UserId user_id,
    std::chrono::system_clock::time_point deleted_at) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(transaction_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    try {
        constexpr const char* kLockAccountSql = R"SQL(
            SELECT a.id
            FROM accounts a
            JOIN transactions t
              ON t.account_id = a.id AND t.user_id = a.user_id
            WHERE t.id = $1 AND t.user_id = $2 AND t.deleted_at IS NULL
            FOR UPDATE OF a NOWAIT
        )SQL";
        const auto locked_account = (*context)->transaction().execSqlSync(
            kLockAccountSql, id.value(), user_id.value());
        if (locked_account.empty()) {
            return std::unexpected(transaction_not_found());
        }

        constexpr const char* kSql = R"SQL(
            UPDATE transactions SET
                deleted_at = $1, updated_at = $1, version = version + 1
            WHERE id = $2 AND user_id = $3 AND deleted_at IS NULL
            RETURNING account_id
        )SQL";
        const auto result = (*context)->transaction().execSqlSync(
            kSql,
            pg::toDbTimestamp(deleted_at),
            id.value(),
            user_id.value());
        if (result.empty()) {
            return std::unexpected(transaction_not_found());
        }
        invalidate_balance_cache(
            (*context)->transaction(),
            domain::AccountId(pg::getBigInt(result[0], 0)),
            tenant_user_id_);
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(
            postgres::database_error("soft-delete transaction", error));
    } catch (const std::exception& error) {
        return std::unexpected(
            postgres::unexpected_error("soft-delete transaction", error));
    }
}

domain::RepositoryResult<domain::TransactionCorrectionPersistResult>
TransactionRepositoryImpl::save_correction(
    domain::ITransactionContext& tx_iface,
    domain::TransactionId original_id,
    const domain::Transaction& replacement,
    std::chrono::system_clock::time_point corrected_at) {
    if (replacement.user_id() != tenant_user_id_) {
        return std::unexpected(transaction_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) return std::unexpected(context.error());

    try {
        auto original = find_by_id_for_update(tx_iface, original_id);
        if (!original) return std::unexpected(original.error());
        if (original->is_deleted()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Transaction is already deleted"));
        }
        if (original->type() == domain::TransactionType::Transfer ||
            original->transfer_group_id().has_value()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Transfer aggregate members cannot be corrected independently"));
        }

        const auto existing_link = (*context)->transaction().execSqlSync(
            "SELECT 1 FROM transaction_corrections "
            "WHERE original_transaction_id = $1 AND user_id = $2",
            original_id.value(), tenant_user_id_.value());
        if (!existing_link.empty()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Transaction is already corrected"));
        }

        auto saved = save_single(tx_iface, replacement);
        if (!saved) return std::unexpected(saved.error());
        auto deleted = soft_delete(
            tx_iface, original_id, tenant_user_id_, corrected_at);
        if (!deleted) return std::unexpected(deleted.error());

        (*context)->transaction().execSqlSync(
            "INSERT INTO transaction_corrections "
            "(original_transaction_id, replacement_transaction_id, user_id, corrected_at) "
            "VALUES ($1, $2, $3, $4)",
            original_id.value(), saved->id().value(), tenant_user_id_.value(),
            pg::toDbTimestamp(corrected_at));
        return domain::TransactionCorrectionPersistResult{*saved};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "save transaction correction", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "save transaction correction", error));
    }
}

domain::RepositoryVoidResult
TransactionRepositoryImpl::physical_delete_by_account(
    domain::ITransactionContext& tx_iface,
    domain::AccountId account_id) {
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    try {
        constexpr const char* kSql = R"SQL(
            WITH target_transactions AS MATERIALIZED (
                SELECT id
                FROM transactions
                WHERE account_id = $1 AND user_id = $2
                  AND transfer_group_id IS NULL
            ),
            locked_account AS MATERIALIZED (
                SELECT id
                FROM accounts
                WHERE id = $1 AND user_id = $2
                FOR UPDATE NOWAIT
            ),
            deleted_balance_cache AS (
                DELETE FROM account_balance_cache
                WHERE account_id = $1 AND user_id = $2
                  AND EXISTS (SELECT 1 FROM locked_account)
                RETURNING account_id
            ),
            deleted_tag_relations AS (
                DELETE FROM transaction_tag_relations
                WHERE user_id = $2
                  AND transaction_id IN (
                      SELECT id FROM target_transactions)
                RETURNING transaction_id
            )
            DELETE FROM transactions
            WHERE user_id = $2
              AND id IN (SELECT id FROM target_transactions)
              AND EXISTS (SELECT 1 FROM locked_account)
              AND (SELECT COUNT(*) FROM deleted_balance_cache) >= 0
              AND (SELECT COUNT(*) FROM deleted_tag_relations) >= 0
        )SQL";
        (*context)->transaction().execSqlSync(
            kSql, account_id.value(), tenant_user_id_.value());
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "delete account transactions", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "delete account transactions", error));
    }
}

domain::RepositoryVoidResult
TransactionRepositoryImpl::physical_delete_transfers_touching_account(
    domain::ITransactionContext& tx_iface,
    domain::AccountId account_id) {
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    try {
        // Capture groups and every affected account before deleting rows. This
        // includes an account that participates only through a grouped fee.
        constexpr const char* kSql = R"SQL(
            WITH target_groups AS MATERIALIZED (
                SELECT DISTINCT transfer_group_id
                FROM transactions
                WHERE account_id = $1 AND user_id = $2
                  AND transfer_group_id IS NOT NULL
            ),
            target_transactions AS MATERIALIZED (
                SELECT id, account_id, transfer_group_id
                FROM transactions
                WHERE transfer_group_id IN (
                    SELECT transfer_group_id FROM target_groups)
                  AND user_id = $2
            ),
            affected_accounts AS MATERIALIZED (
                SELECT DISTINCT account_id
                FROM target_transactions
            ),
            locked_accounts AS MATERIALIZED (
                SELECT a.id
                FROM accounts a
                JOIN affected_accounts affected ON affected.account_id = a.id
                WHERE a.user_id = $2
                ORDER BY a.id
                FOR UPDATE OF a NOWAIT
            ),
            deleted_balance_cache AS (
                DELETE FROM account_balance_cache
                WHERE account_id IN (
                    SELECT account_id FROM affected_accounts)
                  AND user_id = $2
                  AND (SELECT COUNT(*) FROM locked_accounts) =
                      (SELECT COUNT(*) FROM affected_accounts)
                RETURNING account_id
            ),
            deleted_tag_relations AS (
                DELETE FROM transaction_tag_relations
                WHERE user_id = $2
                  AND transaction_id IN (
                      SELECT id FROM target_transactions)
                  AND (SELECT COUNT(*) FROM locked_accounts) =
                      (SELECT COUNT(*) FROM affected_accounts)
                  AND (SELECT COUNT(*) FROM deleted_balance_cache) >= 0
                RETURNING transaction_id
            ),
            deleted_transactions AS (
                DELETE FROM transactions
                WHERE id IN (SELECT id FROM target_transactions)
                  AND user_id = $2
                  AND (SELECT COUNT(*) FROM locked_accounts) =
                      (SELECT COUNT(*) FROM affected_accounts)
                  AND (SELECT COUNT(*) FROM deleted_balance_cache) >= 0
                  AND (SELECT COUNT(*) FROM deleted_tag_relations) >= 0
                RETURNING transfer_group_id
            ),
            deleted_groups AS (
                DELETE FROM transfer_groups
                WHERE id IN (
                    SELECT transfer_group_id FROM target_groups)
                  AND user_id = $2
                  AND (SELECT COUNT(*) FROM deleted_transactions) >= 0
                RETURNING id
            )
            SELECT COUNT(*) AS deleted_group_count
            FROM deleted_groups
        )SQL";
        (*context)->transaction().execSqlSync(
            kSql, account_id.value(), tenant_user_id_.value());
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "delete transfer aggregates", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "delete transfer aggregates", error));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
