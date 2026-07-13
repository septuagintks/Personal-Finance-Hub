// Personal Finance Hub - In-Memory Category Repository
// Version: 1.0
// C++23
//
// Models the `categories` table for repository/report tests without a DB.
// Read-your-writes: staged (uncommitted) rows shadow committed ones so a
// category saved earlier in the same transaction is visible to later reads.

#pragma once

#include "pfh/domain/repositories/i_category_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

class InMemoryCategoryRepository final : public domain::ICategoryRepository {
public:
    explicit InMemoryCategoryRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<domain::Category> find_by_id_for_user(
        domain::CategoryId id,
        domain::UserId user_id) override {
        auto cat = lookup(id.value());
        if (!cat.has_value()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category not found: " + id.to_string()));
        }
        if (cat->owner() != user_id || cat->is_deleted()) {
            // Do not leak existence across users; deleted rows read as absent.
            return std::unexpected(domain::RepositoryError::not_found(
                "Category not found for user"));
        }
        return *cat;
    }

    [[nodiscard]] domain::RepositoryResult<domain::Category>
    find_by_id_for_user_including_deleted(
        domain::CategoryId id,
        domain::UserId user_id) override {
        auto category = lookup(id.value());
        if (!category.has_value() || category->owner() != user_id) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category not found for user"));
        }
        return *category;
    }

    [[nodiscard]] domain::RepositoryResult<domain::Category>
    find_by_id_for_user_for_update(
        domain::ITransactionContext& /*tx*/,
        domain::CategoryId id,
        domain::UserId user_id) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "find_by_id_for_user_for_update requires an active transaction"));
        }
        return find_by_id_for_user(id, user_id);
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Category>> find_by_board(
        domain::UserId user_id,
        domain::CategoryBoard board) override {
        std::vector<domain::Category> result;
        for (const auto& [_, cat] : merged()) {
            if (cat.owner() == user_id && !cat.is_deleted() && cat.board() == board) {
                result.push_back(cat);
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.sort_order() != rhs.sort_order()) {
                return lhs.sort_order() < rhs.sort_order();
            }
            return lhs.id() < rhs.id();
        });
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Category>> find_all_for_user(
        domain::UserId user_id) override {
        std::vector<domain::Category> result;
        for (const auto& [_, cat] : merged()) {
            if (cat.owner() == user_id && !cat.is_deleted()) {
                result.push_back(cat);
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.board() != rhs.board()) {
                return static_cast<int>(lhs.board()) < static_cast<int>(rhs.board());
            }
            if (lhs.sort_order() != rhs.sort_order()) {
                return lhs.sort_order() < rhs.sort_order();
            }
            return lhs.id() < rhs.id();
        });
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<domain::CategoryId> resolve_root_id_for_user(
        domain::CategoryId id,
        domain::UserId user_id) override {
        auto current = find_by_id_for_user_including_deleted(id, user_id);
        if (!current) {
            return std::unexpected(current.error());
        }
        // Walk parents to the root, guarding against cycles / broken chains with
        // a bound (a user cannot realistically have this many nesting levels).
        domain::Category node = *current;
        for (int depth = 0; depth < domain::kMaxCategoryTreeDepth; ++depth) {
            if (node.is_root()) {
                return node.id();
            }
            auto parent = find_by_id_for_user_including_deleted(
                *node.parent_id(), user_id);
            if (!parent) {
                return std::unexpected(domain::RepositoryError::database(
                    "Broken category parent chain for " + id.to_string()));
            }
            node = *parent;
        }
        return std::unexpected(domain::RepositoryError::database(
            "Category parent chain too deep (possible cycle) for " + id.to_string()));
    }

    [[nodiscard]] domain::RepositoryResult<domain::SystemCategoryTemplate>
    find_template_by_id(
        std::int64_t template_id,
        const std::string& locale) override {
        const auto found = store_.category_templates.find(template_id);
        if (found == store_.category_templates.end() ||
            (found->second.locale != locale && found->second.locale != "zh-CN")) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category template not found"));
        }
        return found->second;
    }

    [[nodiscard]] domain::RepositoryResult<domain::CategoryId> save(
        domain::ITransactionContext& /*tx*/,
        const domain::Category& category) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "save requires an active transaction"));
        }
        if (!user_exists(category.owner())) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Category owner not found"));
        }
        if (category.name().empty()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Category name must not be empty"));
        }
        if (category.parent_id().has_value()) {
            if (category.id().is_valid() && *category.parent_id() == category.id()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Category cannot be its own parent"));
            }
            auto parent = find_by_id_for_user(*category.parent_id(), category.owner());
            if (!parent) {
                return std::unexpected(parent.error());
            }
            if (parent->board() != category.board()) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Parent category must use the same board"));
            }
            std::set<std::int64_t> visited;
            auto ancestor = *parent;
            bool reached_root = false;
            for (int ancestor_depth = 1;
                 ancestor_depth < domain::kMaxCategoryTreeDepth;
                 ++ancestor_depth) {
                if ((category.id().is_valid() && ancestor.id() == category.id()) ||
                    !visited.insert(ancestor.id().value()).second) {
                    return std::unexpected(domain::RepositoryError::validation(
                        "Category parent would create a cycle"));
                }
                if (!ancestor.parent_id().has_value()) {
                    reached_root = true;
                    break;
                }
                auto next = find_by_id_for_user(
                    *ancestor.parent_id(), category.owner());
                if (!next) {
                    return std::unexpected(next.error());
                }
                ancestor = *next;
            }
            if (!reached_root) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Category tree cannot exceed " +
                    std::to_string(domain::kMaxCategoryTreeDepth) + " levels"));
            }
        }

        for (const auto& [id, existing] : merged()) {
            if (category.id().is_valid() && id == category.id().value()) {
                continue;
            }
            if (existing.owner() == category.owner() &&
                existing.board() == category.board() &&
                existing.parent_id() == category.parent_id() &&
                existing.name() == category.name()) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Duplicate category name in the same board and parent"));
            }
        }

        // Create path: invalid id => assign a new one.
        if (!category.id().is_valid()) {
            const auto id_value = store_.next_category_id++;
            domain::Category created(
                domain::CategoryId(id_value),
                category.owner(),
                category.name(),
                category.board(),
                category.parent_id(),
                category.source(),
                category.template_id(),
                category.sort_order(),
                category.deleted_at(),
                category.created_at(),
                category.updated_at());
            store_.staged_categories.emplace(id_value, std::move(created));
            return domain::CategoryId(id_value);
        }

        // Update path: category must already exist (committed or staged).
        const auto id = category.id().value();
        const auto existing = lookup(id);
        if (!existing.has_value()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Cannot update unknown category: " + category.id().to_string()));
        }
        if (existing->owner() != category.owner()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Category owner cannot change"));
        }
        if (existing->board() != category.board()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Category board cannot change"));
        }
        store_.staged_categories.insert_or_assign(id, category);
        return category.id();
    }

    [[nodiscard]] domain::RepositoryVoidResult soft_delete(
        domain::ITransactionContext& /*tx*/,
        domain::CategoryId id,
        domain::UserId user_id,
        std::chrono::system_clock::time_point deleted_at) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "soft_delete requires an active transaction"));
        }
        auto category = find_by_id_for_user(id, user_id);
        if (!category) {
            return std::unexpected(category.error());
        }
        for (const auto& [_, candidate] : merged()) {
            if (!candidate.is_deleted() &&
                candidate.parent_id() == std::optional<domain::CategoryId>(id)) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Category has active child categories"));
            }
        }
        domain::Category deleted(
            category->id(), category->owner(), category->name(), category->board(),
            category->parent_id(), category->source(), category->template_id(),
            category->sort_order(), deleted_at, category->created_at(), deleted_at);
        store_.staged_categories.insert_or_assign(id.value(), std::move(deleted));
        return {};
    }

private:
    // Committed rows overlaid with staged inserts/updates and staged deletes,
    // so reads within an active transaction see this transaction's own writes.
    [[nodiscard]] std::map<std::int64_t, domain::Category> merged() const {
        std::map<std::int64_t, domain::Category> m = store_.categories;
        if (store_.in_transaction) {
            for (const auto& [id, cat] : store_.staged_categories) {
                m.insert_or_assign(id, cat);
            }
            for (const auto id : store_.staged_deleted_categories) {
                m.erase(id);
            }
        }
        return m;
    }

    [[nodiscard]] std::optional<domain::Category> lookup(std::int64_t id) const {
        if (store_.in_transaction) {
            for (const auto deleted : store_.staged_deleted_categories) {
                if (deleted == id) {
                    return std::nullopt;
                }
            }
        }
        if (store_.in_transaction) {
            if (auto it = store_.staged_categories.find(id);
                it != store_.staged_categories.end()) {
                return it->second;
            }
        }
        if (auto it = store_.categories.find(id); it != store_.categories.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool user_exists(domain::UserId user_id) const {
        if (store_.in_transaction && store_.staged_users.contains(user_id.value())) {
            return true;
        }
        return store_.users.contains(user_id.value());
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
