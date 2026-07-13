// Personal Finance Hub - In-Memory Tag Repository

#pragma once

#include "pfh/domain/repositories/i_tag_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace pfh::infrastructure {

class InMemoryTagRepository final : public domain::ITagRepository {
public:
    explicit InMemoryTagRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Tag>> find_by_user(
        domain::UserId user_id,
        bool include_deleted = false) override {
        std::vector<domain::Tag> result;
        for (const auto& [_, tag] : merged_tags()) {
            if (tag.owner() == user_id && (include_deleted || !tag.is_deleted())) {
                result.push_back(tag);
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.name() != rhs.name()) return lhs.name() < rhs.name();
            return lhs.id() < rhs.id();
        });
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<domain::Tag> find_by_id_for_user(
        domain::TagId tag_id,
        domain::UserId user_id) override {
        const auto tags = merged_tags();
        const auto found = tags.find(tag_id.value());
        if (found == tags.end() || found->second.owner() != user_id ||
            found->second.is_deleted()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Tag not found for user"));
        }
        return found->second;
    }

    [[nodiscard]] domain::RepositoryResult<domain::Tag>
    find_by_id_for_user_for_update(
        domain::ITransactionContext& /*tx*/,
        domain::TagId tag_id,
        domain::UserId user_id) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "Tag lock requires an active transaction"));
        }
        return find_by_id_for_user(tag_id, user_id);
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Tag>>
    find_by_transaction(
        domain::TransactionId transaction_id,
        domain::UserId user_id) override {
        const auto transaction = lookup_transaction(transaction_id.value());
        if (!transaction.has_value() || transaction->user_id() != user_id ||
            transaction->is_deleted()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found for user"));
        }
        const auto relations = relations_for(transaction_id.value());
        const auto tags = merged_tags();
        std::vector<domain::Tag> result;
        for (const auto tag_id : relations) {
            const auto found = tags.find(tag_id);
            if (found != tags.end() && found->second.owner() == user_id &&
                !found->second.is_deleted()) {
                result.push_back(found->second);
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.name() != rhs.name()) return lhs.name() < rhs.name();
            return lhs.id() < rhs.id();
        });
        return result;
    }

    [[nodiscard]] domain::RepositoryResult<domain::TagId> save(
        domain::ITransactionContext& /*tx*/,
        const domain::Tag& tag) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "Tag save requires an active transaction"));
        }
        if (tag.name().empty() || tag.name().size() > 64) {
            return std::unexpected(domain::RepositoryError::validation(
                "Tag name is invalid"));
        }
        if (!store_.users.contains(tag.owner().value()) &&
            !store_.staged_users.contains(tag.owner().value())) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Tag owner not found"));
        }
        for (const auto& [id, existing] : merged_tags()) {
            if ((!tag.id().is_valid() || id != tag.id().value()) &&
                existing.owner() == tag.owner() && existing.name() == tag.name()) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Tag name already exists for user"));
            }
        }
        if (tag.id().is_valid()) {
            return std::unexpected(domain::RepositoryError::validation(
                "Tag updates are not supported"));
        }
        const auto id = domain::TagId(store_.next_tag_id++);
        store_.staged_tags.emplace(
            id.value(),
            domain::Tag(
                id, tag.owner(), tag.name(), tag.deleted_at(),
                tag.created_at(), tag.updated_at()));
        return id;
    }

    [[nodiscard]] domain::RepositoryVoidResult soft_delete(
        domain::ITransactionContext& /*tx*/,
        domain::TagId tag_id,
        domain::UserId user_id,
        std::chrono::system_clock::time_point deleted_at) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "Tag soft delete requires an active transaction"));
        }
        auto tag = find_by_id_for_user(tag_id, user_id);
        if (!tag) {
            return std::unexpected(tag.error());
        }
        store_.staged_tags.insert_or_assign(
            tag_id.value(),
            domain::Tag(
                tag_id, user_id, tag->name(), deleted_at,
                tag->created_at(), deleted_at));
        return {};
    }

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Tag>>
    replace_transaction_tags(
        domain::ITransactionContext& /*tx*/,
        domain::TransactionId transaction_id,
        domain::UserId user_id,
        const std::vector<domain::TagId>& tag_ids) override {
        if (!store_.in_transaction) {
            return std::unexpected(domain::RepositoryError::database(
                "Tag relation replacement requires an active transaction"));
        }
        const auto transaction = lookup_transaction(transaction_id.value());
        if (!transaction.has_value() || transaction->user_id() != user_id ||
            transaction->is_deleted()) {
            return std::unexpected(domain::RepositoryError::not_found(
                "Transaction not found for user"));
        }
        std::set<std::int64_t> replacement;
        std::vector<domain::Tag> resolved;
        resolved.reserve(tag_ids.size());
        for (const auto tag_id : tag_ids) {
            auto tag = find_by_id_for_user(tag_id, user_id);
            if (!tag) {
                return std::unexpected(tag.error());
            }
            if (!replacement.insert(tag_id.value()).second) {
                return std::unexpected(domain::RepositoryError::validation(
                    "Duplicate tag id"));
            }
            resolved.push_back(std::move(*tag));
        }
        store_.staged_transaction_tag_relations.insert_or_assign(
            transaction_id.value(), std::move(replacement));
        return resolved;
    }

private:
    [[nodiscard]] std::map<std::int64_t, domain::Tag> merged_tags() const {
        auto tags = store_.tags;
        if (store_.in_transaction) {
            for (const auto& [id, tag] : store_.staged_tags) {
                tags.insert_or_assign(id, tag);
            }
            for (const auto id : store_.staged_deleted_tags) {
                tags.erase(id);
            }
        }
        return tags;
    }

    [[nodiscard]] std::optional<domain::Transaction> lookup_transaction(
        std::int64_t id) const {
        if (store_.in_transaction) {
            if (auto found = store_.staged_transactions.find(id);
                found != store_.staged_transactions.end()) {
                return found->second;
            }
        }
        if (auto found = store_.transactions.find(id);
            found != store_.transactions.end()) {
            return found->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::set<std::int64_t> relations_for(std::int64_t id) const {
        if (store_.in_transaction) {
            if (auto found = store_.staged_transaction_tag_relations.find(id);
                found != store_.staged_transaction_tag_relations.end()) {
                return found->second;
            }
        }
        if (auto found = store_.transaction_tag_relations.find(id);
            found != store_.transaction_tag_relations.end()) {
            return found->second;
        }
        return {};
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
