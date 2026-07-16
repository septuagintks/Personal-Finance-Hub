// Personal Finance Hub - Drogon Unit of Work Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/drogon_unit_of_work.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/events/i_domain_event.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/infrastructure/persistence/drogon_transaction_context.h"
#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <utility>

namespace pfh::infrastructure {

DrogonUnitOfWork::DrogonUnitOfWork(
    drogon::orm::DbClientPtr db,
    std::optional<domain::UserId> user_id)
    : db_(std::move(db)), user_id_(user_id) {}

void DrogonUnitOfWork::register_event(std::shared_ptr<domain::IDomainEvent> event) {
    if (event) {
        pending_events_.push_back(std::move(event));
    }
}

domain::RepositoryVoidResult DrogonUnitOfWork::execute_in_transaction(
    std::function<domain::RepositoryVoidResult(domain::ITransactionContext&)> action) {
    if (in_transaction_) {
        return std::unexpected(domain::RepositoryError::database(
            "Nested transactions are not supported"));
    }

    in_transaction_ = true;
    pending_events_.clear();
    auto result = postgres::execute_transaction<void>(
        db_,
        user_id_,
        "unit of work",
        [&](const std::shared_ptr<drogon::orm::Transaction>& transaction)
            -> domain::RepositoryVoidResult {
            DrogonTransactionContext context(transaction, user_id_);
            auto action_result = action(context);
            if (!action_result.has_value()) {
                return action_result;
            }
            return write_outbox(context);
        });

    pending_events_.clear();
    in_transaction_ = false;
    return result;
}

domain::RepositoryVoidResult DrogonUnitOfWork::execute_bootstrap_transaction(
    std::function<domain::RepositoryVoidResult(
        application::ITenantBootstrapTransaction&)> action) {
    if (user_id_.has_value()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Bootstrap transaction must start without a tenant"));
    }
    if (in_transaction_) {
        return std::unexpected(domain::RepositoryError::database(
            "Nested transactions are not supported"));
    }

    in_transaction_ = true;
    pending_events_.clear();
    auto result = postgres::execute_transaction<void>(
        db_,
        std::nullopt,
        "bootstrap unit of work",
        [&](const std::shared_ptr<drogon::orm::Transaction>& transaction)
            -> domain::RepositoryVoidResult {
            DrogonTransactionContext context(transaction, std::nullopt);
            auto action_result = action(context);
            if (!action_result.has_value()) {
                return action_result;
            }
            return write_outbox(context);
        });

    pending_events_.clear();
    in_transaction_ = false;
    return result;
}

domain::RepositoryVoidResult DrogonUnitOfWork::write_outbox(
    domain::ITransactionContext& tx_iface) {
    auto context = postgres::require_transaction(tx_iface, user_id_);
    if (!context.has_value()) {
        return std::unexpected(context.error());
    }
    auto& db_tx = (*context)->transaction();

    if (pending_events_.empty()) {
        return {};
    }

    constexpr const char* kInsertSql = R"SQL(
        INSERT INTO domain_events_outbox (
            id, event_name, aggregate_type, aggregate_id, payload, occurred_at)
        SELECT gen_random_uuid(), event.data->>'eventName',
               event.data->>'aggregateType', event.data->>'aggregateId',
               (event.data->>'payload')::jsonb,
               TIMESTAMPTZ 'epoch' +
                   (event.data->>'occurredAtMicros')::bigint *
                   INTERVAL '1 microsecond'
        FROM jsonb_array_elements($1::jsonb) WITH ORDINALITY AS event(data, position)
        ORDER BY event.position
    )SQL";

    try {
        nlohmann::json batch = nlohmann::json::array();
        for (const auto& evt : pending_events_) {
            const auto occurred_at_micros =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    evt->occurred_at().time_since_epoch()).count();
            batch.push_back({
                {"eventName", evt->event_name()},
                {"aggregateType", evt->aggregate_type()},
                {"aggregateId", evt->aggregate_id()},
                {"payload", evt->payload_json()},
                {"occurredAtMicros", occurred_at_micros}});
        }
        db_tx.execSqlSync(kInsertSql, batch.dump());
        return {};
    } catch (const drogon::orm::DrogonDbException& e) {
        return std::unexpected(postgres::database_error("outbox insert", e));
    } catch (const std::exception& e) {
        return std::unexpected(postgres::unexpected_error("outbox insert", e));
    }
}

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
