// Personal Finance Hub - PostgreSQL Tag Repository

#pragma once

#include "pfh/domain/repositories/i_tag_repository.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <utility>

namespace pfh::infrastructure {

class TagRepositoryImpl final : public domain::ITagRepository {
public:
    TagRepositoryImpl(
        drogon::orm::DbClientPtr db,
        domain::UserId tenant_user_id)
        : db_(std::move(db)), tenant_user_id_(tenant_user_id) {}

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Tag>> find_by_user(
        domain::UserId user_id,
        bool include_deleted = false) override;

    [[nodiscard]] domain::RepositoryResult<domain::Tag> find_by_id_for_user(
        domain::TagId tag_id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Tag>
    find_by_id_for_user_for_update(
        domain::ITransactionContext& tx,
        domain::TagId tag_id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Tag>
    find_by_id_for_user_including_deleted_for_update(
        domain::ITransactionContext& tx,
        domain::TagId tag_id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::Tag> find_by_name_for_update(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        const std::string& name) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Tag>>
    find_by_transaction(
        domain::TransactionId transaction_id,
        domain::UserId user_id) override;

    [[nodiscard]] domain::RepositoryResult<domain::TagId> save(
        domain::ITransactionContext& tx,
        const domain::Tag& tag) override;

    [[nodiscard]] domain::RepositoryVoidResult soft_delete(
        domain::ITransactionContext& tx,
        domain::TagId tag_id,
        domain::UserId user_id,
        std::chrono::system_clock::time_point deleted_at) override;

    [[nodiscard]] domain::RepositoryResult<std::vector<domain::Tag>>
    replace_transaction_tags(
        domain::ITransactionContext& tx,
        domain::TransactionId transaction_id,
        domain::UserId user_id,
        const std::vector<domain::TagId>& tag_ids) override;

private:
    drogon::orm::DbClientPtr db_;
    domain::UserId tenant_user_id_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
