// Personal Finance Hub - Authenticated Finance Application Facade

#include "pfh/application/services/finance_application_service.h"

#include "pfh/application/idempotency.h"
#include "pfh/application/use_cases/account_query_use_cases.h"
#include "pfh/application/use_cases/delete_account_use_case.h"
#include "pfh/application/use_cases/create_transaction_use_case.h"
#include "pfh/application/use_cases/correct_transaction_use_case.h"
#include "pfh/application/use_cases/delete_transaction_use_case.h"
#include "pfh/application/use_cases/create_transfer_use_case.h"
#include "pfh/application/use_cases/get_transfer_use_case.h"
#include "pfh/application/use_cases/transfer_query_use_cases.h"
#include "pfh/application/use_cases/correct_transfer_use_case.h"
#include "pfh/application/use_cases/delete_transfer_use_case.h"
#include "pfh/application/query/report_query_service.h"
#include "pfh/application/use_cases/resource_use_cases.h"
#include "pfh/application/use_cases/transaction_query_use_cases.h"

#include <utility>

namespace pfh::application {

namespace {

void append_optional_time(
    std::string& output,
    const std::optional<std::chrono::system_clock::time_point>& value) {
    append_canonical_field(
        output, value.has_value() ? encode_idempotency_time(*value) : "null");
}

[[nodiscard]] std::string transaction_fingerprint_input(
    const CreateTransactionCommand& command) {
    std::string result;
    result.reserve(256 + command.description.size());
    append_canonical_field(result, "create_transaction:v1");
    append_canonical_field(result, command.user_id.to_string());
    append_canonical_field(result, command.account_id.to_string());
    append_canonical_field(result, std::to_string(static_cast<int>(command.type)));
    append_canonical_field(result, command.amount);
    append_canonical_field(result, command.currency_code);
    append_canonical_field(result, command.description);
    append_canonical_field(
        result, command.category_id.has_value()
            ? command.category_id->to_string() : "null");
    append_optional_time(result, command.occurred_at);
    append_canonical_field(result, std::to_string(command.tag_ids.size()));
    for (const auto tag_id : command.tag_ids) {
        append_canonical_field(result, tag_id.to_string());
    }
    return result;
}

[[nodiscard]] std::string transfer_fingerprint_input(
    const CreateTransferCommand& command) {
    std::string result;
    result.reserve(384 + command.description.size());
    append_canonical_field(result, "create_transfer:v1");
    append_canonical_field(result, command.user_id.to_string());
    append_canonical_field(result, command.source_account_id.to_string());
    append_canonical_field(result, command.target_account_id.to_string());
    append_canonical_field(result, std::to_string(static_cast<int>(command.mode)));
    append_canonical_field(result, command.outgoing_amount);
    append_canonical_field(result, command.incoming_amount);
    append_canonical_field(result, command.rate);
    append_canonical_field(result, command.fee_amount.value_or("null"));
    append_canonical_field(
        result, command.fee_source.has_value()
            ? std::to_string(static_cast<int>(*command.fee_source)) : "null");
    append_canonical_field(
        result, command.fee_account_id.has_value()
            ? command.fee_account_id->to_string() : "null");
    append_canonical_field(result, command.description);
    append_optional_time(result, command.occurred_at);
    return result;
}

[[nodiscard]] std::string correction_fingerprint_input(
    const CorrectTransactionCommand& command) {
    std::string result;
    result.reserve(320 + command.description.size());
    append_canonical_field(result, "correct_transaction:v1");
    append_canonical_field(result, command.user_id.to_string());
    append_canonical_field(
        result, command.original_transaction_id.to_string());
    append_canonical_field(result, command.account_id.to_string());
    append_canonical_field(
        result, std::to_string(static_cast<int>(command.type)));
    append_canonical_field(result, command.amount);
    append_canonical_field(result, command.currency_code);
    append_canonical_field(result, command.description);
    append_canonical_field(
        result, command.category_id.has_value()
            ? command.category_id->to_string() : "null");
    append_optional_time(result, command.occurred_at);
    append_canonical_field(result, std::to_string(command.tag_ids.size()));
    for (const auto tag_id : command.tag_ids) {
        append_canonical_field(result, tag_id.to_string());
    }
    return result;
}

[[nodiscard]] std::string transfer_correction_fingerprint_input(
    const CorrectTransferCommand& command) {
    std::string result;
    result.reserve(448 + command.replacement.description.size());
    append_canonical_field(result, "correct_transfer:v1");
    append_canonical_field(
        result, command.original_transfer_group_id.to_string());
    append_canonical_field(
        result, transfer_fingerprint_input(command.replacement));
    return result;
}

} // namespace

Result<std::unique_ptr<IRequestScope>> FinanceApplicationService::open_scope(
    domain::UserId user_id) {
    if (!user_id.is_valid()) {
        return err(Error::unauthorized());
    }
    auto scope = scopes_.create(user_id);
    if (!scope || scope->user_id() != user_id) {
        return err(Error::infrastructure_failure(
            "Authenticated request scope could not be created"));
    }
    return scope;
}

Result<std::vector<AccountDto>> FinanceApplicationService::list_accounts(
    domain::UserId user_id,
    AccountListStatus status) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    return ListAccountsUseCase((*scope)->accounts()).execute(user_id, status);
}

Result<AccountDto> FinanceApplicationService::get_account(
    domain::UserId user_id,
    domain::AccountId account_id) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    return GetAccountUseCase((*scope)->accounts()).execute(user_id, account_id);
}

Result<AccountDto> FinanceApplicationService::create_account(
    const CreateAccountCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return CreateAccountUseCase(
        (*scope)->accounts(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<BalanceDto> FinanceApplicationService::account_balance(
    domain::UserId user_id,
    domain::AccountId account_id) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    return GetAccountBalanceUseCase((*scope)->accounts()).execute(user_id, account_id);
}

VoidResult FinanceApplicationService::archive_account(
    const ArchiveAccountCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return ArchiveAccountUseCase(
        (*scope)->accounts(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

VoidResult FinanceApplicationService::delete_account(
    const DeleteAccountCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return DeleteAccountUseCase(
        (*scope)->accounts(),
        (*scope)->transactions(),
        (*scope)->unit_of_work(),
        (*scope)->audit_logs())
        .execute(command);
}

Result<std::vector<CategoryTreeDto>> FinanceApplicationService::list_categories(
    domain::UserId user_id,
    std::optional<domain::CategoryBoard> board,
    MetadataListStatus status) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    return ListCategoriesUseCase((*scope)->categories()).execute(user_id, board, status);
}

Result<CategoryDto> FinanceApplicationService::create_category(
    const CreateCategoryCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return CreateCategoryUseCase(
        (*scope)->categories(),
        (*scope)->preferences(),
        (*scope)->audit_logs(),
        (*scope)->unit_of_work())
        .execute(command);
}

VoidResult FinanceApplicationService::delete_category(
    const DeleteCategoryCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return DeleteCategoryUseCase(
        (*scope)->categories(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<CategoryDto> FinanceApplicationService::update_category(
    const UpdateCategoryCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return UpdateCategoryUseCase(
        (*scope)->categories(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<CategoryDto> FinanceApplicationService::restore_category(
    const RestoreCategoryCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return RestoreCategoryUseCase(
        (*scope)->categories(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<std::vector<TagDto>> FinanceApplicationService::list_tags(
    domain::UserId user_id,
    MetadataListStatus status) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    return ListTagsUseCase((*scope)->tags()).execute(user_id, status);
}

Result<TagDto> FinanceApplicationService::create_tag(
    const CreateTagCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return CreateTagUseCase(
        (*scope)->tags(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

VoidResult FinanceApplicationService::delete_tag(
    const DeleteTagCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return DeleteTagUseCase(
        (*scope)->tags(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<std::vector<TagDto>> FinanceApplicationService::replace_transaction_tags(
    const ReplaceTransactionTagsCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return ReplaceTransactionTagsUseCase(
        (*scope)->tags(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<UserPreferenceDto> FinanceApplicationService::get_preferences(
    domain::UserId user_id) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    return GetUserPreferenceUseCase((*scope)->preferences()).execute(user_id);
}

Result<UserPreferenceDto> FinanceApplicationService::update_preferences(
    const UpdateUserPreferenceCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return UpdateUserPreferenceUseCase(
        (*scope)->preferences(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<std::vector<CurrencyMetadataDto>>
FinanceApplicationService::list_currencies() const {
    return ListCurrenciesUseCase().execute();
}

Result<TransactionDto> FinanceApplicationService::create_transaction(
    const CreateTransactionCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return CreateTransactionUseCase(
        (*scope)->accounts(),
        (*scope)->categories(),
        (*scope)->transactions(),
        (*scope)->unit_of_work(),
        nullptr,
        &(*scope)->tags())
        .execute(command);
}

Result<CursorPage<TransactionDto>> FinanceApplicationService::list_transactions(
    const TransactionListQuery& query) {
    auto scope = open_scope(query.user_id);
    if (!scope) return err(scope.error());
    return ListTransactionsUseCase((*scope)->transactions()).execute(query);
}

Result<TransactionDto> FinanceApplicationService::get_transaction(
    domain::UserId user_id,
    domain::TransactionId transaction_id) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    return GetTransactionUseCase((*scope)->transactions()).execute(
        user_id, transaction_id);
}

Result<TransactionDto> FinanceApplicationService::correct_transaction(
    const CorrectTransactionCommand& command,
    std::string_view idempotency_key) {
    auto fingerprint = request_hasher_.sha256(
        correction_fingerprint_input(command));
    if (!fingerprint) return err(fingerprint.error());
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return CorrectTransactionUseCase(
        (*scope)->accounts(),
        (*scope)->categories(),
        (*scope)->tags(),
        (*scope)->transactions(),
        (*scope)->audit_logs(),
        (*scope)->unit_of_work(),
        clock_,
        &(*scope)->idempotency())
        .execute(command, IdempotencyRequest{
            std::string(idempotency_key), *fingerprint, clock_.now()});
}

Result<TagDto> FinanceApplicationService::update_tag(
    const UpdateTagCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return UpdateTagUseCase(
        (*scope)->tags(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<TagDto> FinanceApplicationService::restore_tag(
    const RestoreTagCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return RestoreTagUseCase(
        (*scope)->tags(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

VoidResult FinanceApplicationService::restore_account(
    const RestoreAccountCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return RestoreAccountUseCase(
        (*scope)->accounts(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<AccountDto> FinanceApplicationService::update_account(
    const UpdateAccountCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return UpdateAccountUseCase(
        (*scope)->accounts(), (*scope)->audit_logs(), (*scope)->unit_of_work())
        .execute(command);
}

Result<TransactionDto> FinanceApplicationService::create_transaction(
    const CreateTransactionCommand& command,
    std::string_view idempotency_key) {
    auto fingerprint = request_hasher_.sha256(
        transaction_fingerprint_input(command));
    if (!fingerprint) return err(fingerprint.error());
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return CreateTransactionUseCase(
        (*scope)->accounts(),
        (*scope)->categories(),
        (*scope)->transactions(),
        (*scope)->unit_of_work(),
        &(*scope)->idempotency(),
        &(*scope)->tags())
        .execute(command, IdempotencyRequest{
            std::string(idempotency_key), *fingerprint, clock_.now()});
}

VoidResult FinanceApplicationService::delete_transaction(
    const DeleteTransactionCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return DeleteTransactionUseCase(
        (*scope)->transactions(),
        (*scope)->audit_logs(),
        (*scope)->unit_of_work())
        .execute(command);
}

Result<TransferResultDto> FinanceApplicationService::create_transfer(
    const CreateTransferCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return CreateTransferUseCase(
        (*scope)->accounts(),
        (*scope)->transactions(),
        (*scope)->unit_of_work())
        .execute(command);
}

Result<TransferResultDto> FinanceApplicationService::create_transfer(
    const CreateTransferCommand& command,
    std::string_view idempotency_key) {
    auto fingerprint = request_hasher_.sha256(
        transfer_fingerprint_input(command));
    if (!fingerprint) return err(fingerprint.error());
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return CreateTransferUseCase(
        (*scope)->accounts(),
        (*scope)->transactions(),
        (*scope)->unit_of_work(),
        &(*scope)->idempotency())
        .execute(command, IdempotencyRequest{
            std::string(idempotency_key), *fingerprint, clock_.now()});
}

Result<TransferResultDto> FinanceApplicationService::get_transfer(
    domain::UserId user_id,
    domain::TransferGroupId group_id) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    return GetTransferUseCase((*scope)->transactions()).execute(user_id, group_id);
}

Result<CursorPage<TransferResultDto>> FinanceApplicationService::list_transfers(
    const TransferListQuery& query) {
    auto scope = open_scope(query.user_id);
    if (!scope) return err(scope.error());
    return ListTransfersUseCase((*scope)->transactions()).execute(query);
}

Result<TransferResultDto> FinanceApplicationService::correct_transfer(
    const CorrectTransferCommand& command,
    std::string_view idempotency_key) {
    auto fingerprint = request_hasher_.sha256(
        transfer_correction_fingerprint_input(command));
    if (!fingerprint) return err(fingerprint.error());
    auto scope = open_scope(command.replacement.user_id);
    if (!scope) return err(scope.error());
    return CorrectTransferUseCase(
        (*scope)->accounts(),
        (*scope)->transactions(),
        (*scope)->audit_logs(),
        (*scope)->unit_of_work(),
        clock_,
        &(*scope)->idempotency())
        .execute(command, IdempotencyRequest{
            std::string(idempotency_key), *fingerprint, clock_.now()});
}

VoidResult FinanceApplicationService::delete_transfer(
    const DeleteTransferCommand& command) {
    auto scope = open_scope(command.user_id);
    if (!scope) return err(scope.error());
    return DeleteTransferUseCase(
        (*scope)->accounts(),
        (*scope)->transactions(),
        (*scope)->audit_logs(),
        (*scope)->unit_of_work(),
        clock_)
        .execute(command);
}

Result<NetWorthDto> FinanceApplicationService::net_worth(
    domain::UserId user_id) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    ReportQueryService reports(
        (*scope)->accounts(),
        (*scope)->transactions(),
        (*scope)->exchange_rates(),
        (*scope)->preferences(),
        &(*scope)->categories());
    return reports.net_worth(user_id, clock_.now());
}

Result<CashFlowTrendDto> FinanceApplicationService::cash_flow_trend(
    const CashFlowTrendQuery& query) {
    auto scope = open_scope(query.user_id);
    if (!scope) return err(scope.error());
    ReportQueryService reports(
        (*scope)->accounts(),
        (*scope)->transactions(),
        (*scope)->exchange_rates(),
        (*scope)->preferences(),
        &(*scope)->categories());
    return reports.cash_flow_trend(
        query.user_id,
        query.start_year,
        query.start_month,
        query.end_year,
        query.end_month);
}

Result<DashboardSummaryDto> FinanceApplicationService::dashboard_summary(
    domain::UserId user_id) {
    auto scope = open_scope(user_id);
    if (!scope) return err(scope.error());
    ReportQueryService reports(
        (*scope)->accounts(),
        (*scope)->transactions(),
        (*scope)->exchange_rates(),
        (*scope)->preferences(),
        &(*scope)->categories());
    return reports.dashboard_summary(user_id, clock_.now());
}

} // namespace pfh::application
