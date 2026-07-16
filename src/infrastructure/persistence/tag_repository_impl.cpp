// Personal Finance Hub - PostgreSQL Tag Repository

#include "pfh/infrastructure/persistence/tag_repository_impl.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <utility>

namespace pfh::infrastructure {

namespace {

constexpr const char* kTagColumns =
    "id, user_id, name, deleted_at, created_at, updated_at";

[[nodiscard]] domain::RepositoryError tag_not_found() {
    return domain::RepositoryError::not_found("Tag not found for user");
}

[[nodiscard]] domain::RepositoryResult<domain::Tag> map_tag(
    const drogon::orm::Row& row) {
    try {
        return domain::Tag(
            domain::TagId(pg::getBigInt(row, 0)),
            domain::UserId(pg::getBigInt(row, 1)),
            pg::getString(row, 2),
            pg::getOptionalTimestamp(row, 3),
            pg::getTimestamp(row, 4),
            pg::getTimestamp(row, 5));
    } catch (const std::exception&) {
        return std::unexpected(domain::RepositoryError::database(
            "Stored tag row is invalid"));
    }
}

[[nodiscard]] bool is_unique_violation(
    const drogon::orm::DrogonDbException& error) {
    const std::string detail = error.base().what();
    return detail.find("23505") != std::string::npos ||
           detail.find("uq_transaction_tags_user_name") != std::string::npos;
}

} // namespace

domain::RepositoryResult<std::vector<domain::Tag>>
TagRepositoryImpl::find_by_user(
    domain::UserId user_id,
    bool include_deleted) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(tag_not_found());
    }
    return postgres::execute_tenant_read<std::vector<domain::Tag>>(
        db_, tenant_user_id_, "list tags", [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") + kTagColumns +
                " FROM transaction_tags WHERE user_id = $1" +
                (include_deleted ? std::string{} : " AND deleted_at IS NULL") +
                " ORDER BY name, id";
            const auto rows = transaction->execSqlSync(sql, user_id.value());
            std::vector<domain::Tag> result;
            result.reserve(rows.size());
            for (const auto& row : rows) {
                auto tag = map_tag(row);
                if (!tag) {
                    return domain::RepositoryResult<std::vector<domain::Tag>>(
                        std::unexpected(tag.error()));
                }
                result.push_back(std::move(*tag));
            }
            return domain::RepositoryResult<std::vector<domain::Tag>>(
                std::move(result));
        });
}

domain::RepositoryResult<domain::Tag> TagRepositoryImpl::find_by_id_for_user(
    domain::TagId tag_id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(tag_not_found());
    }
    return postgres::execute_tenant_read<domain::Tag>(
        db_, tenant_user_id_, "find tag", [&](const auto& transaction) {
            const std::string sql = std::string("SELECT ") + kTagColumns +
                " FROM transaction_tags WHERE id = $1 AND user_id = $2 "
                "AND deleted_at IS NULL";
            const auto rows = transaction->execSqlSync(
                sql, tag_id.value(), user_id.value());
            return rows.empty()
                ? domain::RepositoryResult<domain::Tag>(
                      std::unexpected(tag_not_found()))
                : map_tag(rows[0]);
        });
}

domain::RepositoryResult<domain::Tag>
TagRepositoryImpl::find_by_id_for_user_for_update(
    domain::ITransactionContext& tx_iface,
    domain::TagId tag_id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(tag_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) {
        return std::unexpected(context.error());
    }
    try {
        const std::string sql = std::string("SELECT ") + kTagColumns +
            " FROM transaction_tags WHERE id = $1 AND user_id = $2 "
            "AND deleted_at IS NULL FOR UPDATE NOWAIT";
        const auto rows = (*context)->transaction().execSqlSync(
            sql, tag_id.value(), user_id.value());
        return rows.empty()
            ? domain::RepositoryResult<domain::Tag>(
                  std::unexpected(tag_not_found()))
            : map_tag(rows[0]);
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error("lock tag", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("lock tag", error));
    }
}

domain::RepositoryResult<std::vector<domain::Tag>>
TagRepositoryImpl::find_by_transaction(
    domain::TransactionId transaction_id,
    domain::UserId user_id) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::not_found(
            "Transaction not found for user"));
    }
    return postgres::execute_tenant_read<std::vector<domain::Tag>>(
        db_, tenant_user_id_, "find transaction tags", [&](const auto& transaction) {
            const auto owned = transaction->execSqlSync(
                "SELECT 1 FROM transactions "
                "WHERE id = $1 AND user_id = $2 AND deleted_at IS NULL",
                transaction_id.value(), user_id.value());
            if (owned.empty()) {
                return domain::RepositoryResult<std::vector<domain::Tag>>(
                    std::unexpected(domain::RepositoryError::not_found(
                        "Transaction not found for user")));
            }
            const std::string sql = std::string("SELECT t.") +
                "id, t.user_id, t.name, t.deleted_at, t.created_at, t.updated_at "
                "FROM transaction_tag_relations r "
                "JOIN transaction_tags t ON t.id = r.tag_id AND t.user_id = r.user_id "
                "WHERE r.transaction_id = $1 AND r.user_id = $2 "
                "AND t.deleted_at IS NULL ORDER BY t.name, t.id";
            const auto rows = transaction->execSqlSync(
                sql, transaction_id.value(), user_id.value());
            std::vector<domain::Tag> result;
            result.reserve(rows.size());
            for (const auto& row : rows) {
                auto tag = map_tag(row);
                if (!tag) {
                    return domain::RepositoryResult<std::vector<domain::Tag>>(
                        std::unexpected(tag.error()));
                }
                result.push_back(std::move(*tag));
            }
            return domain::RepositoryResult<std::vector<domain::Tag>>(
                std::move(result));
        });
}

domain::RepositoryResult<domain::TagId> TagRepositoryImpl::save(
    domain::ITransactionContext& tx_iface,
    const domain::Tag& tag) {
    if (tag.owner() != tenant_user_id_ || tag.id().is_valid() ||
        tag.name().empty() || tag.name().size() > 64) {
        return std::unexpected(domain::RepositoryError::validation(
            "Tag create data is invalid"));
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) {
        return std::unexpected(context.error());
    }
    try {
        const auto rows = (*context)->transaction().execSqlSync(
            "INSERT INTO transaction_tags "
            "(user_id, name, deleted_at, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, $5) RETURNING id",
            tag.owner().value(), tag.name(), pg::toDbTimestamp(tag.deleted_at()),
            pg::toDbTimestamp(tag.created_at()), pg::toDbTimestamp(tag.updated_at()));
        if (rows.empty()) {
            return std::unexpected(domain::RepositoryError::database(
                "Tag insert returned no identifier"));
        }
        return domain::TagId(pg::getBigInt(rows[0], 0));
    } catch (const drogon::orm::DrogonDbException& error) {
        if (is_unique_violation(error)) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Tag name already exists for user"));
        }
        return std::unexpected(postgres::database_error("create tag", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("create tag", error));
    }
}

domain::RepositoryVoidResult TagRepositoryImpl::soft_delete(
    domain::ITransactionContext& tx_iface,
    domain::TagId tag_id,
    domain::UserId user_id,
    std::chrono::system_clock::time_point deleted_at) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(tag_not_found());
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) {
        return std::unexpected(context.error());
    }
    try {
        const auto result = (*context)->transaction().execSqlSync(
            "UPDATE transaction_tags SET deleted_at = $1, updated_at = $1 "
            "WHERE id = $2 AND user_id = $3 AND deleted_at IS NULL",
            pg::toDbTimestamp(deleted_at), tag_id.value(), user_id.value());
        return result.affectedRows() == 0
            ? domain::RepositoryVoidResult(std::unexpected(tag_not_found()))
            : domain::RepositoryVoidResult{};
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error("soft delete tag", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error("soft delete tag", error));
    }
}

domain::RepositoryResult<std::vector<domain::Tag>>
TagRepositoryImpl::replace_transaction_tags(
    domain::ITransactionContext& tx_iface,
    domain::TransactionId transaction_id,
    domain::UserId user_id,
    const std::vector<domain::TagId>& tag_ids) {
    if (user_id != tenant_user_id_) {
        return std::unexpected(domain::RepositoryError::not_found(
            "Transaction not found for user"));
    }
    if (tag_ids.size() > domain::kMaxTagsPerTransaction) {
        return std::unexpected(domain::RepositoryError::validation(
            "A transaction can have at most 64 tags"));
    }
    auto context = postgres::require_transaction(tx_iface, tenant_user_id_);
    if (!context) {
        return std::unexpected(context.error());
    }
    try {
        const auto transaction = (*context)->transaction().execSqlSync(
            "SELECT 1 FROM transactions "
            "WHERE id = $1 AND user_id = $2 AND deleted_at IS NULL FOR UPDATE",
            transaction_id.value(), user_id.value());
        if (transaction.empty()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found for user"));
        }

        std::set<std::int64_t> unique;
        nlohmann::json requested_ids = nlohmann::json::array();
        for (const auto tag_id : tag_ids) {
            if (!tag_id.is_valid() || !unique.insert(tag_id.value()).second) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Tag ids must be unique positive integers"));
            }
            requested_ids.push_back(tag_id.value());
        }

        constexpr const char* kResolveTagsSql = R"SQL(
            SELECT tag.id, tag.user_id, tag.name, tag.deleted_at,
                   tag.created_at, tag.updated_at
            FROM jsonb_array_elements_text($1::jsonb) WITH ORDINALITY
                 AS requested(id, position)
            JOIN transaction_tags tag ON tag.id = requested.id::bigint
            WHERE tag.user_id = $2 AND tag.deleted_at IS NULL
            ORDER BY requested.position
            FOR SHARE OF tag
        )SQL";
        const auto tags = (*context)->transaction().execSqlSync(
            kResolveTagsSql, requested_ids.dump(), user_id.value());
        if (tags.size() != tag_ids.size()) {
            return std::unexpected(tag_not_found());
        }

        std::vector<domain::Tag> resolved;
        resolved.reserve(tags.size());
        for (const auto& tag : tags) {
            auto mapped = map_tag(tag);
            if (!mapped) {
                return std::unexpected(mapped.error());
            }
            resolved.push_back(std::move(*mapped));
        }

        (*context)->transaction().execSqlSync(
            "DELETE FROM transaction_tag_relations "
            "WHERE transaction_id = $1 AND user_id = $2",
            transaction_id.value(),
            user_id.value());
        if (!tag_ids.empty()) {
            constexpr const char* kInsertRelationsSql = R"SQL(
                INSERT INTO transaction_tag_relations (
                    transaction_id, tag_id, user_id)
                SELECT $1, requested.id::bigint, $2
                FROM jsonb_array_elements_text($3::jsonb) AS requested(id)
            )SQL";
            (*context)->transaction().execSqlSync(
                kInsertRelationsSql,
                transaction_id.value(),
                user_id.value(),
                requested_ids.dump());
        }
        return resolved;
    } catch (const drogon::orm::DrogonDbException& error) {
        return std::unexpected(postgres::database_error(
            "replace transaction tags", error));
    } catch (const std::exception& error) {
        return std::unexpected(postgres::unexpected_error(
            "replace transaction tags", error));
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
