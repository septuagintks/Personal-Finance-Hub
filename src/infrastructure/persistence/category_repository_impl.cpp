// Personal Finance Hub - PostgreSQL Category Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/category_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/repositories/repository_error.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

namespace pfh::infrastructure {

namespace {

domain::RepositoryResult<domain::Category> map_category_row(
    const drogon::orm::Row& row) {
    try {
        const auto id = domain::CategoryId(pg::getBigInt(row, 0));
        const auto user_id = domain::UserId(pg::getBigInt(row, 1));
        const auto name = pg::getString(row, 2);
        const auto parent_id_val = pg::getOptionalBigInt(row, 3);
        const auto board = pg::parseCategoryBoard(pg::getString(row, 4));
        const auto source = pg::parseCategorySource(pg::getString(row, 5));
        const auto template_id = pg::getOptionalBigInt(row, 6);
        const auto sort_order = static_cast<int>(pg::getBigInt(row, 7));
        const auto deleted_at = pg::getOptionalTimestamp(row, 8);
        const auto created_at = pg::getTimestamp(row, 9);
        const auto updated_at = pg::getTimestamp(row, 10);

        std::optional<domain::CategoryId> parent_id;
        if (parent_id_val.has_value()) {
            parent_id = domain::CategoryId(*parent_id_val);
        }

        return domain::Category(
            id, user_id, name, board, parent_id, source, template_id,
            sort_order, deleted_at, created_at, updated_at);
    } catch (const std::exception& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("map_category_row: ") + e.what()));
    }
}

constexpr const char* kSelectColumns = R"SQL(
    id, user_id, name, parent_id, board::text, source::text, template_id,
    sort_order, deleted_at, created_at, updated_at
)SQL";

}  // namespace

domain::RepositoryResult<domain::Category>
CategoryRepositoryImpl::find_by_id_for_user(
    domain::CategoryId id,
    domain::UserId user_id) {
    const std::string sql =
        std::string("SELECT ") + kSelectColumns +
        " FROM categories WHERE id = $1 AND user_id = $2 AND deleted_at IS NULL";
    try {
        auto result = db_->execSqlSync(sql, id.value, user_id.value);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category not found for user"));
        }
        return map_category_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_id_for_user: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::Category>
CategoryRepositoryImpl::find_by_id_for_user_for_update(
    domain::ITransactionContext& tx_iface,
    domain::CategoryId id,
    domain::UserId user_id) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    const std::string sql =
        std::string("SELECT ") + kSelectColumns +
        " FROM categories WHERE id = $1 AND user_id = $2 AND deleted_at IS NULL FOR UPDATE";
    try {
        auto result = tx.execSqlSync(sql, id.value, user_id.value);
        if (result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category not found for user"));
        }
        return map_category_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_id_for_user_for_update: ") + e.base().what()));
    }
}

domain::RepositoryResult<std::vector<domain::Category>>
CategoryRepositoryImpl::find_by_board(
    domain::UserId user_id,
    domain::CategoryBoard board) {
    const std::string sql =
        std::string("SELECT ") + kSelectColumns +
        " FROM categories WHERE user_id = $1 AND board = $2::category_board AND deleted_at IS NULL ORDER BY sort_order, id";
    try {
        auto result = db_->execSqlSync(sql, user_id.value, pg::toSqlText(board));
        std::vector<domain::Category> categories;
        categories.reserve(result.size());
        for (const auto& row : result) {
            auto cat = map_category_row(row);
            if (!cat.has_value()) {
                return std::unexpected(cat.error());
            }
            categories.push_back(std::move(*cat));
        }
        return categories;
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_by_board: ") + e.base().what()));
    }
}

domain::RepositoryResult<std::vector<domain::Category>>
CategoryRepositoryImpl::find_all_for_user(domain::UserId user_id) {
    const std::string sql =
        std::string("SELECT ") + kSelectColumns +
        " FROM categories WHERE user_id = $1 AND deleted_at IS NULL ORDER BY board, sort_order, id";
    try {
        auto result = db_->execSqlSync(sql, user_id.value);
        std::vector<domain::Category> categories;
        categories.reserve(result.size());
        for (const auto& row : result) {
            auto cat = map_category_row(row);
            if (!cat.has_value()) {
                return std::unexpected(cat.error());
            }
            categories.push_back(std::move(*cat));
        }
        return categories;
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("find_all_for_user: ") + e.base().what()));
    }
}

domain::RepositoryResult<domain::CategoryId>
CategoryRepositoryImpl::resolve_root_id_for_user(
    domain::CategoryId id,
    domain::UserId user_id) {
    auto current = find_by_id_for_user(id, user_id);
    if (!current.has_value()) {
        return std::unexpected(current.error());
    }

    constexpr int kMaxDepth = 64;
    domain::CategoryId current_id = id;
    for (int depth = 0; depth < kMaxDepth; ++depth) {
        if (!current->parent_id().has_value()) {
            return current_id;
        }
        current_id = *current->parent_id();
        current = find_by_id_for_user(current_id, user_id);
        if (!current.has_value()) {
            return std::unexpected(domain::RepositoryError::database(
                "Broken category parent chain for " + id.to_string()));
        }
    }
    return std::unexpected(domain::RepositoryError::database(
        "Category parent chain too deep (possible cycle) for " + id.to_string()));
}

domain::RepositoryResult<domain::CategoryId> CategoryRepositoryImpl::save(
    domain::ITransactionContext& tx_iface,
    const domain::Category& category) {
    auto& drogon_ctx = static_cast<DrogonTransactionContext&>(tx_iface);
    auto& tx = drogon_ctx.transaction();

    if (!category.id().is_valid()) {
        // Insert path.
        constexpr const char* kInsertSql = R"SQL(
            INSERT INTO categories (
                user_id, name, parent_id, board, source, template_id,
                sort_order, deleted_at, created_at, updated_at)
            VALUES (
                $1, $2, $3, $4::category_board, $5::category_source, $6,
                $7, $8, $9, $10)
            RETURNING id
        )SQL";
        try {
            std::optional<std::int64_t> parent_val;
            if (category.parent_id().has_value()) {
                parent_val = category.parent_id()->value;
            }
            std::optional<std::int64_t> template_val;
            if (category.template_id().has_value()) {
                template_val = *category.template_id();
            }

            auto result = tx.execSqlSync(
                kInsertSql,
                category.owner().value,
                category.name(),
                parent_val,
                pg::toSqlText(category.board()),
                pg::toSqlText(category.source()),
                template_val,
                category.sort_order(),
                category.deleted_at(),
                category.created_at(),
                category.updated_at());
            if (result.empty()) {
                return std::unexpected(domain::RepositoryError::database(
                    "insert category: no id returned"));
            }
            return domain::CategoryId(pg::getBigInt(result[0], 0));
        } catch (const drogon::orm::DrogonDbException& e) {
            const std::string msg = e.base().what();
            if (msg.find("23505") != std::string::npos) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Duplicate category name in the same board and parent"));
            }
            return std::unexpected(domain::RepositoryError::database(
                std::string("insert category: ") + msg));
        }
    }

    // Update path.
    constexpr const char* kUpdateSql = R"SQL(
        UPDATE categories SET
            name = $1, parent_id = $2, board = $3::category_board,
            source = $4::category_source, template_id = $5, sort_order = $6,
            deleted_at = $7, updated_at = $8
        WHERE id = $9 AND user_id = $10
    )SQL";
    try {
        std::optional<std::int64_t> parent_val;
        if (category.parent_id().has_value()) {
            parent_val = category.parent_id()->value;
        }
        std::optional<std::int64_t> template_val;
        if (category.template_id().has_value()) {
            template_val = *category.template_id();
        }

        auto result = tx.execSqlSync(
            kUpdateSql,
            category.name(),
            parent_val,
            pg::toSqlText(category.board()),
            pg::toSqlText(category.source()),
            template_val,
            category.sort_order(),
            category.deleted_at(),
            category.updated_at(),
            category.id().value,
            category.owner().value);
        if (result.affectedRows() == 0) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Cannot update unknown category: " + category.id().to_string()));
        }
        return category.id();
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(domain::RepositoryError::database(
            std::string("update category: ") + e.base().what()));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
