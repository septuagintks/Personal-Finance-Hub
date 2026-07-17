// Personal Finance Hub - Authenticated User Maintenance Use Cases

#include "pfh/application/use_cases/maintenance_use_cases.h"

#include "pfh/application/error_mapping.h"
#include "pfh/application/persistence_time.h"

#include <optional>
#include <string>
#include <utility>

namespace pfh::application {

Result<UserAuditLogPageDto> ListUserAuditLogsUseCase::execute(
    const UserAuditLogQueryDto& query) {
    if (!query.user_id.is_valid() || query.page_size == 0 ||
        query.page_size > 100 ||
        (query.resource_type.has_value() &&
         (query.resource_type->empty() || query.resource_type->size() > 64)) ||
        (query.before_id.has_value() && *query.before_id <= 0) ||
        (query.from.has_value() && query.to.has_value() &&
         *query.from > *query.to)) {
        return err(Error::validation("Audit query is invalid"));
    }

    std::optional<domain::UserAuditLogPage> page;
    auto read = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto loaded = audit_logs_.find_user_entries(
                tx,
                domain::UserAuditLogQuery{
                    query.user_id,
                    query.action,
                    query.resource_type,
                    query.from,
                    query.to,
                    query.before_id,
                    query.page_size});
            if (!loaded) return std::unexpected(loaded.error());
            page = std::move(*loaded);
            return {};
        });
    if (!read) return err(from_repository(read.error()));
    if (!page.has_value()) {
        return err(Error::infrastructure_failure(
            "Audit query result was not produced"));
    }

    UserAuditLogPageDto result;
    result.items.reserve(page->entries.size());
    for (const auto& entry : page->entries) {
        result.items.push_back(UserAuditLogItemDto{
            entry.id,
            entry.action,
            entry.resource_type,
            entry.resource_id,
            "success",
            entry.trace_id.empty()
                ? std::nullopt
                : std::optional<std::string>(entry.trace_id),
            entry.occurred_at});
    }
    if (page->next_before_id.has_value()) {
        result.next_cursor = std::to_string(*page->next_before_id);
    }
    return result;
}

Result<BalanceCacheRebuildDto> RebuildBalanceCacheUseCase::execute(
    const RebuildBalanceCacheCommand& command) {
    if (!command.user_id.is_valid() ||
        (command.account_id.has_value() && !command.account_id->is_valid()) ||
        command.trace_id.empty() || command.trace_id.size() > 128) {
        return err(Error::validation("Balance cache rebuild request is invalid"));
    }
    const auto now = normalize_persisted_time(clock_.now());
    std::optional<std::vector<domain::BalanceCacheRebuildResult>> rebuilt;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto result = accounts_.rebuild_balance_cache(
                tx, command.user_id, command.account_id, now);
            if (!result) return std::unexpected(result.error());
            for (const auto& item : *result) {
                domain::AuditLogEntry audit;
                audit.operator_user_id = command.user_id;
                audit.actor_type = domain::AuditActorType::User;
                audit.action = domain::AuditAction::Refresh;
                audit.resource_type = "BalanceCache";
                audit.resource_id = item.snapshot.account_id.to_string();
                audit.after_value_json =
                    "{\"sourceVersion\":" +
                    std::to_string(item.source_version) +
                    ",\"cacheVersion\":" +
                    std::to_string(item.cache_version) + "}";
                audit.metadata_json = "{}";
                audit.trace_id = command.trace_id;
                audit.occurred_at = now;
                if (auto appended = audit_logs_.append(tx, audit); !appended) {
                    return appended;
                }
            }
            rebuilt = std::move(*result);
            return {};
        });
    if (!write) return err(from_repository(write.error()));
    if (!rebuilt.has_value()) {
        return err(Error::infrastructure_failure(
            "Balance cache rebuild result was not produced"));
    }

    BalanceCacheRebuildDto result;
    result.accounts.reserve(rebuilt->size());
    for (const auto& item : *rebuilt) {
        result.accounts.push_back(BalanceCacheRebuildItemDto{
            item.snapshot.account_id,
            item.snapshot.balance.currency().code(),
            item.snapshot.balance.amount().to_string(),
            item.snapshot.last_transaction_id.is_valid()
                ? std::optional<domain::TransactionId>(
                      item.snapshot.last_transaction_id)
                : std::nullopt,
            item.source_version,
            item.cache_version,
            item.snapshot.as_of});
    }
    return result;
}

} // namespace pfh::application
