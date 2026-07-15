// Personal Finance Hub - In-Memory Request Idempotency

#pragma once

#include "pfh/application/ports/i_idempotency_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include "pfh/infrastructure/persistence/in_memory_transaction_context.h"

#include <string>

namespace pfh::infrastructure {

class InMemoryIdempotencyRepository final
    : public application::IIdempotencyRepository {
public:
    explicit InMemoryIdempotencyRepository(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] domain::RepositoryResult<application::IdempotencyBeginResult>
    begin(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        std::string_view operation,
        std::string_view key,
        std::string_view request_fingerprint,
        std::chrono::system_clock::time_point created_at,
        std::chrono::system_clock::time_point expires_at) override {
        auto* context = dynamic_cast<InMemoryTransactionContext*>(&tx);
        if (context == nullptr || !store_.in_transaction ||
            context->tenant_user_id() != std::optional<domain::UserId>(user_id)) {
            return std::unexpected(domain::RepositoryError::validation(
                "Idempotency access requires the matching tenant transaction"));
        }
        const auto composite = make_key(user_id, operation, key);
        auto staged = store_.staged_idempotency.find(composite);
        auto committed = store_.idempotency.find(composite);
        const InMemoryIdempotencyRecord* record = staged != store_.staged_idempotency.end()
            ? &staged->second
            : (committed != store_.idempotency.end() &&
                       !store_.staged_deleted_idempotency.contains(composite)
                   ? &committed->second
                   : nullptr);
        if (record != nullptr && record->expires_at <= created_at) {
            store_.staged_idempotency.erase(composite);
            store_.staged_deleted_idempotency.insert(composite);
            record = nullptr;
        }
        if (record != nullptr) {
            if (record->request_fingerprint != request_fingerprint) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Idempotency key was already used with another request"));
            }
            if (!record->completed) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Idempotent request is still in progress"));
            }
            return application::IdempotencyBeginResult{
                true, record->response_values};
        }

        InMemoryIdempotencyRecord inserted;
        inserted.user_id = user_id;
        inserted.operation = std::string(operation);
        inserted.key = std::string(key);
        inserted.request_fingerprint = std::string(request_fingerprint);
        inserted.created_at = created_at;
        inserted.expires_at = expires_at;
        store_.staged_idempotency.emplace(composite, std::move(inserted));
        return application::IdempotencyBeginResult{};
    }

    [[nodiscard]] domain::RepositoryVoidResult complete(
        domain::ITransactionContext& tx,
        domain::UserId user_id,
        std::string_view operation,
        std::string_view key,
        const std::map<std::string, std::string>& response_values) override {
        auto* context = dynamic_cast<InMemoryTransactionContext*>(&tx);
        if (context == nullptr || !store_.in_transaction ||
            context->tenant_user_id() != std::optional<domain::UserId>(user_id)) {
            return std::unexpected(domain::RepositoryError::validation(
                "Idempotency completion requires the matching tenant transaction"));
        }
        const auto composite = make_key(user_id, operation, key);
        auto record = store_.staged_idempotency.find(composite);
        if (record == store_.staged_idempotency.end() || record->second.completed) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Idempotency request is not pending"));
        }
        record->second.response_values = response_values;
        record->second.completed = true;
        return {};
    }

private:
    [[nodiscard]] static std::string make_key(
        domain::UserId user_id,
        std::string_view operation,
        std::string_view key) {
        return user_id.to_string() + "\n" + std::string(operation) + "\n" +
               std::string(key);
    }

    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
