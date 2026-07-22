// Personal Finance Hub - PostgreSQL Category Repository Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/category_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"
#include "pfh/domain/resource_limits.h"

#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

namespace {

constexpr const char* kCategoryColumns = R"SQL(
    id, user_id, name, parent_id, board::text, source::text, template_id,
    sort_order, deleted_at, created_at, updated_at
)SQL";

domain::RepositoryError category_not_found() {
    return domain::RepositoryError::not_found("Category not found for user");
}

domain::RepositoryResult<domain::Category> map_category_row(
    const drogon::orm::Row& row) {
    try {
        const auto raw_sort_order = pg::getBigInt(row, 7);
        if (raw_sort_order < std::numeric_limits<int>::min() ||
            raw_sort_order > std::numeric_limits<int>::max()) {
            return std::unexpected(domain::RepositoryError::database(
                "Stored category sort order is invalid"));
        }

        std::optional<domain::CategoryId> parent_id;
        const auto parent_value = pg::getOptionalBigInt(row, 3);
        if (parent_value.has_value()) {
            parent_id = domain::CategoryId(*parent_value);
        }

        return domain::Category(
            domain::CategoryId(pg::getBigInt(row, 0)),
            domain::UserId(pg::getBigInt(row, 1)),
            pg::getString(row, 2),
            pg::parseCategoryBoard(pg::getString(row, 4)),
            parent_id,
            pg::parseCategorySource(pg::getString(row, 5)),
            pg::getOptionalBigInt(row, 6),
            static_cast<int>(raw_sort_order),
            pg::getOptionalTimestamp(row, 8),
            pg::getTimestamp(row, 9),
            pg::getTimestamp(row, 10));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored category row is invalid"));
    }
}

bool is_unique_violation(const drogon::orm::DrogonDbException& error) {
    const std::string detail = error.base().what();
    return detail.find("23505") != std::string::npos ||
           detail.find("categories_user_id_board_parent_id_name") !=
               std::string::npos;
}

}  // namespace

domain::RepositoryResult<domain::Category>
CategoryRepositoryImpl::find_by_id_for_user(
    domain::CategoryId id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    return postgres::execute_tenant_read<domain::Category>(
        db_, tenant_user_id_, "find category", [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") + kCategoryColumns +
                " FROM categories WHERE id = $1 AND user_id = $2 "
                "AND deleted_at IS NULL";
            const auto result = transaction->execSqlSync(
                sql, id.value(), user_id.value());
            if (result.empty()) {
                return domain::RepositoryResult<domain::Category>(
                    std::unexpected(category_not_found()));
            }
            return map_category_row(result[0]);
        });
}

domain::RepositoryResult<domain::Category>
CategoryRepositoryImpl::find_by_id_for_user_including_deleted(
    domain::CategoryId id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    return postgres::execute_tenant_read<domain::Category>(
        db_, tenant_user_id_, "find historical category",
        [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") + kCategoryColumns +
                " FROM categories WHERE id = $1 AND user_id = $2";
            const auto result = transaction->execSqlSync(
                sql, id.value(), user_id.value());
            if (result.empty()) {
                return domain::RepositoryResult<domain::Category>(
                    std::unexpected(category_not_found()));
            }
            return map_category_row(result[0]);
        });
}

domain::RepositoryResult<domain::Category>
CategoryRepositoryImpl::find_by_id_for_user_for_update(
    domain::ITransactionContext& tx_iface,
    domain::CategoryId id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    const std::string sql = std::string("SELECT ") + kCategoryColumns +
        " FROM categories WHERE id = $1 AND user_id = $2 "
        "AND deleted_at IS NULL FOR UPDATE NOWAIT";
    try {
        const auto result = (*context)->transaction().execSqlSync(
            sql, id.value(), user_id.value());
        if (result.empty()) {
            return std::unexpected(category_not_found());
        }
        return map_category_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error("lock category", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("lock category", error));
    }
}

domain::RepositoryResult<domain::Category>
CategoryRepositoryImpl::find_by_id_for_user_including_deleted_for_update(
    domain::ITransactionContext& tx_iface,
    domain::CategoryId id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) return std::unexpected(context.error());
    const std::string sql = std::string("SELECT ") + kCategoryColumns +
        " FROM categories WHERE id = $1 AND user_id = $2 FOR UPDATE NOWAIT";
    try {
        const auto result = (*context)->transaction().execSqlSync(
            sql, id.value(), user_id.value());
        return result.empty()
            ? domain::RepositoryResult<domain::Category>(
                  std::unexpected(category_not_found()))
            : map_category_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "lock historical category", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "lock historical category", error));
    }
}

domain::RepositoryResult<domain::Category>
CategoryRepositoryImpl::find_identity_for_update(
    domain::ITransactionContext& tx_iface,
    domain::UserId user_id,
    domain::CategoryBoard board,
    const std::optional<domain::CategoryId>& parent_id,
    const std::string& name,
    const std::optional<std::int64_t>& template_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) return std::unexpected(context.error());
    const std::optional<std::int64_t> parent_value = parent_id.has_value()
        ? std::optional<std::int64_t>(parent_id->value()) : std::nullopt;
    try {
        const auto owner = (*context)->transaction().execSqlSync(
            "SELECT 1 FROM users WHERE id = $1 FOR UPDATE", user_id.value());
        if (owner.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category owner not found"));
        }
        const auto result = [&]() {
            if (template_id.has_value()) {
                const std::string sql = std::string("SELECT ") + kCategoryColumns +
                    " FROM categories WHERE user_id = $1 "
                    "AND board = $2::category_board "
                    "AND parent_id IS NOT DISTINCT FROM $3 "
                    "AND template_id = $4 FOR UPDATE NOWAIT";
                return (*context)->transaction().execSqlSync(
                    sql, user_id.value(), pg::toSqlText(board), parent_value,
                    *template_id);
            }
            const std::string sql = std::string("SELECT ") + kCategoryColumns +
                " FROM categories WHERE user_id = $1 "
                "AND board = $2::category_board "
                "AND parent_id IS NOT DISTINCT FROM $3 "
                "AND name = $4 FOR UPDATE NOWAIT";
            return (*context)->transaction().execSqlSync(
                sql, user_id.value(), pg::toSqlText(board), parent_value, name);
        }();
        return result.empty()
            ? domain::RepositoryResult<domain::Category>(
                  std::unexpected(domain::RepositoryError::not_found(
                      "Category identity not found for user")))
            : map_category_row(result[0]);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "lock category identity", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "lock category identity", error));
    }
}

domain::RepositoryResult<std::vector<domain::Category>>
CategoryRepositoryImpl::find_by_board(
    domain::UserId user_id,
    domain::CategoryBoard board) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    return postgres::execute_tenant_read<std::vector<domain::Category>>(
        db_, tenant_user_id_, "list category board", [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") + kCategoryColumns +
                " FROM categories WHERE user_id = $1 "
                "AND board = $2::category_board AND deleted_at IS NULL "
                "ORDER BY sort_order, id";
            const auto result = transaction->execSqlSync(
                sql, user_id.value(), pg::toSqlText(board));
            std::vector<domain::Category> categories;
            categories.reserve(result.size());
            for (const auto& row : result) {
                auto category = map_category_row(row);
                if (!category.has_value()) {
                    return domain::RepositoryResult<std::vector<domain::Category>>(
                        std::unexpected(category.error()));
                }
                categories.push_back(std::move(*category));
            }
            return domain::RepositoryResult<std::vector<domain::Category>>(
                std::move(categories));
        });
}

domain::RepositoryResult<std::vector<domain::Category>>
CategoryRepositoryImpl::find_all_for_user(domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    return postgres::execute_tenant_read<std::vector<domain::Category>>(
        db_, tenant_user_id_, "list categories", [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") + kCategoryColumns +
                " FROM categories WHERE user_id = $1 AND deleted_at IS NULL "
                "ORDER BY board, sort_order, id";
            const auto result = transaction->execSqlSync(sql, user_id.value());
            std::vector<domain::Category> categories;
            categories.reserve(result.size());
            for (const auto& row : result) {
                auto category = map_category_row(row);
                if (!category.has_value()) {
                    return domain::RepositoryResult<std::vector<domain::Category>>(
                        std::unexpected(category.error()));
                }
                categories.push_back(std::move(*category));
            }
            return domain::RepositoryResult<std::vector<domain::Category>>(
                std::move(categories));
        });
}

domain::RepositoryResult<std::vector<domain::Category>>
CategoryRepositoryImpl::find_all_for_user_including_deleted(
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    return postgres::execute_tenant_read<std::vector<domain::Category>>(
        db_, tenant_user_id_, "list historical categories",
        [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") + kCategoryColumns +
                " FROM categories WHERE user_id = $1 ORDER BY id";
            const auto result = transaction->execSqlSync(sql, user_id.value());
            std::vector<domain::Category> categories;
            categories.reserve(result.size());
            for (const auto& row : result) {
                auto category = map_category_row(row);
                if (!category) {
                    return domain::RepositoryResult<
                        std::vector<domain::Category>>(
                            std::unexpected(category.error()));
                }
                categories.push_back(std::move(*category));
            }
            return domain::RepositoryResult<std::vector<domain::Category>>(
                std::move(categories));
        });
}

domain::RepositoryResult<domain::CategoryId>
CategoryRepositoryImpl::resolve_root_id_for_user(
    domain::CategoryId id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    return postgres::execute_tenant_read<domain::CategoryId>(
        db_, tenant_user_id_, "resolve category root", [&](const auto& transaction) {
            constexpr const char* kSql = R"SQL(
                SELECT parent_id
                FROM categories
                WHERE id = $1 AND user_id = $2
            )SQL";
            std::set<std::int64_t> visited;
            auto current = id;
            for (int depth = 0; depth < domain::kMaxCategoryTreeDepth; ++depth) {
                if (!visited.insert(current.value()).second) {
                    return domain::RepositoryResult<domain::CategoryId>(
                        std::unexpected(domain::RepositoryError::database(
                            "Category parent cycle detected")));
                }
                const auto result = transaction->execSqlSync(
                    kSql, current.value(), user_id.value());
                if (result.empty()) {
                    if (depth == 0) {
                        return domain::RepositoryResult<domain::CategoryId>(
                            std::unexpected(category_not_found()));
                    }
                    return domain::RepositoryResult<domain::CategoryId>(
                        std::unexpected(domain::RepositoryError::database(
                            "Category parent chain is broken")));
                }
                const auto parent = pg::getOptionalBigInt(result[0], 0);
                if (!parent.has_value()) {
                    return domain::RepositoryResult<domain::CategoryId>(current);
                }
                current = domain::CategoryId(*parent);
            }
            return domain::RepositoryResult<domain::CategoryId>(
                std::unexpected(domain::RepositoryError::database(
                    "Category parent chain exceeds 64 levels")));
        });
}

domain::RepositoryResult<domain::SystemCategoryTemplate>
CategoryRepositoryImpl::find_template_by_id(
    std::int64_t template_id,
    const std::string& locale) {
    if (template_id <= 0 || locale.empty()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Category template id and locale are required"));
    }
    return postgres::execute_tenant_read<domain::SystemCategoryTemplate>(
        db_, tenant_user_id_, "find category template", [&](const auto& transaction) {
            constexpr const char* kSql = R"SQL(
                SELECT id, name, locale, group_name, parent_id,
                       default_board::text, sort_order, is_selectable
                FROM system_category_templates
                WHERE id = $1 AND (locale = $2 OR locale = 'zh-CN')
                ORDER BY CASE WHEN locale = $2 THEN 0 ELSE 1 END
                LIMIT 1
            )SQL";
            const auto result = transaction->execSqlSync(kSql, template_id, locale);
            if (result.empty()) {
                return domain::RepositoryResult<domain::SystemCategoryTemplate>(
                    std::unexpected(domain::RepositoryError::not_found(
                        "Category template not found")));
            }
            try {
                const auto raw_sort_order = pg::getBigInt(result[0], 6);
                if (raw_sort_order < std::numeric_limits<int>::min() ||
                    raw_sort_order > std::numeric_limits<int>::max()) {
                    return domain::RepositoryResult<domain::SystemCategoryTemplate>(
                        std::unexpected(domain::RepositoryError::database(
                            "Stored category template sort order is invalid")));
                }
                domain::SystemCategoryTemplate value;
                value.id = pg::getBigInt(result[0], 0);
                value.name = pg::getString(result[0], 1);
                value.locale = pg::getString(result[0], 2);
                value.group_name = pg::getString(result[0], 3);
                value.parent_id = pg::getOptionalBigInt(result[0], 4);
                const auto board = pg::getOptionalString(result[0], 5);
                if (board.has_value()) {
                    value.default_board = pg::parseCategoryBoard(*board);
                }
                value.sort_order = static_cast<int>(raw_sort_order);
                value.is_selectable = pg::getBool(result[0], 7);
                return domain::RepositoryResult<domain::SystemCategoryTemplate>(
                    std::move(value));
            } catch (const std::exception&) {
                return domain::RepositoryResult<domain::SystemCategoryTemplate>(
                    std::unexpected(domain::RepositoryError::database(
                        "Stored category template row is invalid")));
            }
        });
}

domain::RepositoryResult<domain::CategoryId> CategoryRepositoryImpl::save(
    domain::ITransactionContext& tx_iface,
    const domain::Category& category) {
    if (category.owner() != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::validation(
            "Category owner does not match repository tenant"));
    }
    if (category.name().empty()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Category name must not be empty"));
    }
    if (category.parent_id().has_value() &&
        category.parent_id() == std::optional<domain::CategoryId>(category.id())) {
        return std::unexpected(domain::RepositoryError::validation(
            "Category cannot be its own parent"));
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }

    std::optional<std::int64_t> parent_value;
    if (category.parent_id().has_value()) {
        parent_value = category.parent_id()->value();
    }
    const auto template_value = category.template_id();

    try {
        const auto owner_result = (*context)->transaction().execSqlSync(
            category.id().is_valid()
                ? "SELECT 1 FROM users WHERE id = $1"
                : "SELECT 1 FROM users WHERE id = $1 FOR UPDATE",
            category.owner().value());
        if (owner_result.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category owner not found"));
        }

        if (category.id().is_valid()) {
            constexpr const char* kExistingSql = R"SQL(
                SELECT board::text
                FROM categories
                WHERE id = $1 AND user_id = $2
            )SQL";
            const auto existing = (*context)->transaction().execSqlSync(
                kExistingSql,
                category.id().value(),
                category.owner().value());
            if (existing.empty()) {
                return std::unexpected(category_not_found());
            }
            if (pg::parseCategoryBoard(pg::getString(existing[0], 0)) !=
                category.board()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Category board cannot change"));
            }
        }

        if (category.parent_id().has_value()) {
            constexpr const char* kParentSql = R"SQL(
                SELECT board::text
                FROM categories
                WHERE id = $1 AND user_id = $2 AND deleted_at IS NULL
                FOR UPDATE NOWAIT
            )SQL";
            const auto parent = (*context)->transaction().execSqlSync(
                kParentSql,
                category.parent_id()->value(),
                category.owner().value());
            if (parent.empty()) {
                return std::unexpected(domain::RepositoryError::not_found(
                    "Parent category not found for user"));
            }
            if (pg::parseCategoryBoard(pg::getString(parent[0], 0)) !=
                category.board()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Parent category must use the same board"));
            }
            constexpr const char* kAncestorSql = R"SQL(
                SELECT parent_id
                FROM categories
                WHERE id = $1 AND user_id = $2 AND deleted_at IS NULL
            )SQL";
            const bool updating = category.id().is_valid();
            std::set<std::int64_t> visited;
            auto current = *category.parent_id();
            bool reached_root = false;
            for (int ancestor_depth = 1;
                 ancestor_depth < domain::kMaxCategoryTreeDepth;
                 ++ancestor_depth) {
                if ((updating && current == category.id()) ||
                    !visited.insert(current.value()).second) {
                    return std::unexpected(domain::RepositoryError::validation(
                        "Category parent would create a cycle"));
                }
                const auto ancestor = (*context)->transaction().execSqlSync(
                    kAncestorSql, current.value(), category.owner().value());
                if (ancestor.empty()) {
                    return std::unexpected(domain::RepositoryError::not_found(
                        "Category ancestor not found for user"));
                }
                const auto next = pg::getOptionalBigInt(ancestor[0], 0);
                if (!next.has_value()) {
                    reached_root = true;
                    break;
                }
                current = domain::CategoryId(*next);
            }
            if (!reached_root) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Category tree cannot exceed " +
                    std::to_string(domain::kMaxCategoryTreeDepth) + " levels"));
            }
        }

        if (!category.id().is_valid()) {
            const auto count = (*context)->transaction().execSqlSync(
                "SELECT COUNT(*) FROM categories WHERE user_id = $1",
                category.owner().value());
            if (count.empty()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Category count returned no row"));
            }
            if (static_cast<std::size_t>(pg::getBigInt(count[0], 0)) >=
                domain::kMaximumCategoriesPerUser) {
                return std::unexpected(domain::RepositoryError::resource_limit(
                    "Category limit reached for user"));
            }
            constexpr const char* kInsertSql = R"SQL(
                INSERT INTO categories (
                    user_id, name, parent_id, board, source, template_id,
                    sort_order, deleted_at, created_at, updated_at)
                VALUES (
                    $1, $2, $3, $4::category_board, $5::category_source, $6,
                    $7, $8, $9, $10)
                RETURNING id
            )SQL";
            const auto result = (*context)->transaction().execSqlSync(
                kInsertSql,
                category.owner().value(),
                category.name(),
                parent_value,
                pg::toSqlText(category.board()),
                pg::toSqlText(category.source()),
                template_value,
                category.sort_order(),
                pg::toDbTimestamp(category.deleted_at()),
                pg::toDbTimestamp(category.created_at()),
                pg::toDbTimestamp(category.updated_at()));
            if (result.empty()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Category insert returned no identifier"));
            }
            return domain::CategoryId(pg::getBigInt(result[0], 0));
        }

        constexpr const char* kUpdateSql = R"SQL(
            UPDATE categories SET
                name = $1, parent_id = $2, board = $3::category_board,
                source = $4::category_source, template_id = $5,
                sort_order = $6, deleted_at = $7, updated_at = $8
            WHERE id = $9 AND user_id = $10
        )SQL";
        const auto result = (*context)->transaction().execSqlSync(
            kUpdateSql,
            category.name(),
            parent_value,
            pg::toSqlText(category.board()),
            pg::toSqlText(category.source()),
            template_value,
            category.sort_order(),
            pg::toDbTimestamp(category.deleted_at()),
            pg::toDbTimestamp(category.updated_at()),
            category.id().value(),
            category.owner().value());
        if (result.affectedRows() == 0) {
            return std::unexpected(category_not_found());
        }
        return category.id();
    } catch (const drogon::orm::DrogonDbException& error) {
        if (is_unique_violation(error)) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Category name already exists under the same parent"));
        }
        return std::unexpected(postgres::database_error("save category", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("save category", error));
    }
}

domain::RepositoryVoidResult CategoryRepositoryImpl::soft_delete(
    domain::ITransactionContext& tx_iface,
    domain::CategoryId id,
    domain::UserId user_id,
    std::chrono::system_clock::time_point deleted_at) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(category_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) {
        return std::unexpected(context.error());
    }
    try {
        const auto children = (*context)->transaction().execSqlSync(
            "SELECT 1 FROM categories "
            "WHERE parent_id = $1 AND user_id = $2 AND deleted_at IS NULL LIMIT 1",
            id.value(), user_id.value());
        if (!children.empty()) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Category has active child categories"));
        }
        const auto result = (*context)->transaction().execSqlSync(
            "UPDATE categories SET deleted_at = $1, updated_at = $1 "
            "WHERE id = $2 AND user_id = $3 AND deleted_at IS NULL",
            pg::toDbTimestamp(deleted_at), id.value(), user_id.value());
        if (result.affectedRows() == 0) {
            return std::unexpected(category_not_found());
        }
        return {};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "soft delete category", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "soft delete category", error));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
