// Personal Finance Hub - Authenticated Finance Application Facade

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/persistence/i_request_scope.h"
#include "pfh/application/ports/i_clock.h"
#include "pfh/application/ports/i_request_hasher.h"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace pfh::application {

struct DeleteAccountCommand;

/// @brief Application-layer entry point used by resource controllers.
/// It creates one authenticated scope per operation and keeps repositories and
/// UnitOfWork orchestration out of Presentation.
class FinanceApplicationService {
public:
    FinanceApplicationService(
        IRequestScopeFactory& scopes,
        const IClock& clock,
        const IRequestHasher& request_hasher)
        : scopes_(scopes), clock_(clock), request_hasher_(request_hasher) {}

    [[nodiscard]] Result<std::vector<AccountDto>> list_accounts(
        domain::UserId user_id,
        AccountListStatus status = AccountListStatus::Active);
    [[nodiscard]] Result<AccountDto> get_account(
        domain::UserId user_id,
        domain::AccountId account_id);
    [[nodiscard]] Result<AccountDto> create_account(const CreateAccountCommand& command);
    [[nodiscard]] Result<AccountDto> update_account(const UpdateAccountCommand& command);
    [[nodiscard]] Result<BalanceDto> account_balance(
        domain::UserId user_id,
        domain::AccountId account_id);
    [[nodiscard]] VoidResult archive_account(const ArchiveAccountCommand& command);
    [[nodiscard]] VoidResult restore_account(const RestoreAccountCommand& command);
    [[nodiscard]] VoidResult delete_account(const DeleteAccountCommand& command);

    [[nodiscard]] Result<std::vector<CategoryTreeDto>> list_categories(
        domain::UserId user_id,
        std::optional<domain::CategoryBoard> board,
        MetadataListStatus status = MetadataListStatus::Active);
    [[nodiscard]] Result<CategoryDto> create_category(
        const CreateCategoryCommand& command);
    [[nodiscard]] VoidResult delete_category(const DeleteCategoryCommand& command);
    [[nodiscard]] Result<CategoryDto> update_category(
        const UpdateCategoryCommand& command);
    [[nodiscard]] Result<CategoryDto> restore_category(
        const RestoreCategoryCommand& command);

    [[nodiscard]] Result<std::vector<TagDto>> list_tags(
        domain::UserId user_id,
        MetadataListStatus status = MetadataListStatus::Active);
    [[nodiscard]] Result<TagDto> create_tag(const CreateTagCommand& command);
    [[nodiscard]] VoidResult delete_tag(const DeleteTagCommand& command);
    [[nodiscard]] Result<TagDto> update_tag(const UpdateTagCommand& command);
    [[nodiscard]] Result<TagDto> restore_tag(const RestoreTagCommand& command);
    [[nodiscard]] Result<std::vector<TagDto>> replace_transaction_tags(
        const ReplaceTransactionTagsCommand& command);

    [[nodiscard]] Result<UserPreferenceDto> get_preferences(domain::UserId user_id);
    [[nodiscard]] Result<UserPreferenceDto> update_preferences(
        const UpdateUserPreferenceCommand& command);

    [[nodiscard]] Result<std::vector<CurrencyMetadataDto>> list_currencies() const;

    [[nodiscard]] Result<TransactionDto> create_transaction(
        const CreateTransactionCommand& command);
    [[nodiscard]] Result<TransactionDto> create_transaction(
        const CreateTransactionCommand& command,
        std::string_view idempotency_key);
    [[nodiscard]] Result<CursorPage<TransactionDto>> list_transactions(
        const TransactionListQuery& query);
    [[nodiscard]] Result<TransactionDto> get_transaction(
        domain::UserId user_id,
        domain::TransactionId transaction_id);
    [[nodiscard]] Result<TransactionDto> correct_transaction(
        const CorrectTransactionCommand& command,
        std::string_view idempotency_key);
    [[nodiscard]] VoidResult delete_transaction(
        const DeleteTransactionCommand& command);

    [[nodiscard]] Result<TransferResultDto> create_transfer(
        const CreateTransferCommand& command);
    [[nodiscard]] Result<TransferResultDto> create_transfer(
        const CreateTransferCommand& command,
        std::string_view idempotency_key);
    [[nodiscard]] Result<TransferResultDto> get_transfer(
        domain::UserId user_id,
        domain::TransferGroupId group_id);

    [[nodiscard]] Result<NetWorthDto> net_worth(domain::UserId user_id);
    [[nodiscard]] Result<CashFlowTrendDto> cash_flow_trend(
        const CashFlowTrendQuery& query);
    [[nodiscard]] Result<DashboardSummaryDto> dashboard_summary(
        domain::UserId user_id);

private:
    [[nodiscard]] Result<std::unique_ptr<IRequestScope>> open_scope(
        domain::UserId user_id);

    IRequestScopeFactory& scopes_;
    const IClock& clock_;
    const IRequestHasher& request_hasher_;
};

} // namespace pfh::application
