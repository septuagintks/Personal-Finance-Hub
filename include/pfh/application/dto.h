// Personal Finance Hub - Application DTOs
// Version: 1.0
// C++23
//
// API-facing DTOs. Amounts are always strings at the application boundary.

#pragma once

#include "pfh/domain/account.h"
#include "pfh/domain/audit_log.h"
#include "pfh/domain/category.h"
#include "pfh/domain/money.h"
#include "pfh/domain/tag.h"
#include "pfh/domain/transaction.h"
#include "pfh/domain/transfer_aggregate.h"
#include "pfh/domain/user.h"
#include "pfh/domain/user_preference.h"
#include "pfh/application/pagination.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pfh::application {

struct AccountDto {
    domain::AccountId id;
    domain::UserId owner;
    std::string name;
    domain::AccountType type;
    std::string subtype;
    domain::AccountCategory category;
    std::string currency_code;
    std::string description;
    bool is_archived = false;
    std::optional<std::chrono::system_clock::time_point> archived_at;
    std::chrono::system_clock::time_point created_at{};
    std::chrono::system_clock::time_point updated_at{};
    std::int64_t version = 1;
};

enum class AccountListStatus {
    Active,
    Archived,
    All
};

enum class MetadataListStatus {
    Active,
    Deleted,
    All
};

struct CreateAccountCommand {
    domain::UserId user_id;
    std::string name;
    domain::AccountType type = domain::AccountType::Other;
    std::string subtype;
    std::string currency_code;
    std::string description;
    std::optional<domain::AccountCategory> category;
};

struct UpdateAccountCommand {
    domain::UserId user_id;
    domain::AccountId account_id;
    std::int64_t expected_version = 0;
    std::string name;
    domain::AccountType type = domain::AccountType::Other;
    std::string subtype;
    domain::AccountCategory category = domain::AccountCategory::Asset;
    std::string currency_code;
    std::string description;
};

struct ArchiveAccountCommand {
    domain::UserId user_id;
    domain::AccountId account_id;
    std::int64_t expected_version = 0;
    std::optional<std::chrono::system_clock::time_point> archived_at;
};

struct RestoreAccountCommand {
    domain::UserId user_id;
    domain::AccountId account_id;
    std::int64_t expected_version = 0;
    std::optional<std::chrono::system_clock::time_point> restored_at;
};

struct BalanceDto {
    domain::AccountId account_id;
    std::string currency_code;
    std::string amount; // string, never float/double
    // REST contract (10_REST_API_Design §2.2) exposes these on the balance
    // snapshot. last_transaction_id is nullopt when the account has no
    // transactions yet.
    std::optional<domain::TransactionId> last_transaction_id;
    std::chrono::system_clock::time_point updated_at{};
};

struct TransactionTagDto {
    domain::TagId id;
    std::string name;
    bool is_deleted = false;
};

struct TransactionDto {
    domain::TransactionId id;
    domain::UserId user_id;
    domain::AccountId account_id;
    std::string currency_code;
    std::string amount; // signed string amount as stored
    domain::TransactionType type;
    std::string description;
    std::optional<domain::CategoryId> category_id;
    std::optional<domain::TransferGroupId> transfer_group_id;
    std::chrono::system_clock::time_point occurred_at{};
    std::chrono::system_clock::time_point created_at{};
    std::optional<std::chrono::system_clock::time_point> deleted_at;
    std::optional<std::string> category_name;
    bool category_deleted = false;
    std::vector<TransactionTagDto> tags;
    std::optional<domain::TransactionId> corrects_transaction_id;
    std::optional<domain::TransactionId> corrected_by_transaction_id;
};

struct CreateTransactionCommand {
    domain::UserId user_id;
    domain::AccountId account_id;
    domain::TransactionType type = domain::TransactionType::Expense;
    std::string amount; // string input
    std::string currency_code;
    std::string description;
    std::optional<domain::CategoryId> category_id;
    // Business time. nullopt => the use case stamps the current time. Never
    // default to epoch 0 (1970), which would corrupt current-month reports and
    // point-in-time historical rate selection.
    std::optional<std::chrono::system_clock::time_point> occurred_at;
    std::vector<domain::TagId> tag_ids;
};

struct TransactionListQuery {
    domain::UserId user_id;
    std::optional<domain::AccountId> account_id;
    std::optional<domain::TransactionType> type;
    std::optional<domain::CategoryId> category_id;
    std::optional<domain::TagId> tag_id;
    std::optional<std::chrono::system_clock::time_point> occurred_from;
    std::optional<std::chrono::system_clock::time_point> occurred_to;
    std::string keyword;
    CursorPageRequest page;
};

struct CorrectTransactionCommand {
    domain::UserId user_id;
    domain::TransactionId original_transaction_id;
    domain::AccountId account_id;
    domain::TransactionType type = domain::TransactionType::Expense;
    std::string amount;
    std::string currency_code;
    std::string description;
    std::optional<domain::CategoryId> category_id;
    std::optional<std::chrono::system_clock::time_point> occurred_at;
    std::vector<domain::TagId> tag_ids;
};

struct DeleteTransactionCommand {
    domain::UserId user_id;
    domain::TransactionId transaction_id;
    // nullopt => the use case stamps the current time as the soft-delete time.
    std::optional<std::chrono::system_clock::time_point> deleted_at;
};

enum class TransferInputMode {
    OutgoingAndRate,
    BothAmounts,
    IncomingAndRate
};

struct CreateTransferCommand {
    domain::UserId user_id;
    domain::AccountId source_account_id;
    domain::AccountId target_account_id;
    TransferInputMode mode = TransferInputMode::BothAmounts;
    std::string outgoing_amount; // optional depending on mode
    std::string incoming_amount; // optional depending on mode
    std::string rate;            // optional depending on mode
    // Fee fields are an all-or-nothing contract. fee_amount is a positive
    // magnitude in the selected fee account's currency. ThirdParty additionally
    // requires fee_account_id; SourceAccount/TargetAccount forbid it.
    std::optional<std::string> fee_amount;
    std::optional<domain::FeeSource> fee_source;
    std::optional<domain::AccountId> fee_account_id;
    std::string description;
    // nullopt => the use case stamps the current time. Never epoch 0.
    std::optional<std::chrono::system_clock::time_point> occurred_at;
};

struct TransferResultDto {
    domain::TransferGroupId transfer_group_id;
    TransferInputMode mode = TransferInputMode::BothAmounts;
    domain::AccountId source_account_id;
    domain::AccountId target_account_id;
    domain::TransactionId outgoing_transaction_id;
    domain::TransactionId incoming_transaction_id;
    std::vector<domain::TransactionId> adjustment_transaction_ids;
    std::string outgoing_amount;
    std::string incoming_amount;
    std::string source_currency_code;
    std::string target_currency_code;
    std::optional<std::string> rate;
    std::optional<std::string> fee_amount;
    std::optional<domain::FeeSource> fee_source;
    std::optional<domain::AccountId> fee_account_id;
    std::optional<std::string> fee_currency_code;
    std::string description;
    std::chrono::system_clock::time_point occurred_at{};
    std::chrono::system_clock::time_point created_at{};
    std::optional<std::chrono::system_clock::time_point> deleted_at;
    std::optional<domain::TransferGroupId> corrects_transfer_group_id;
    std::optional<domain::TransferGroupId> corrected_by_transfer_group_id;
};

struct TransferListQuery {
    domain::UserId user_id;
    std::optional<domain::AccountId> account_id;
    std::optional<std::chrono::system_clock::time_point> occurred_from;
    std::optional<std::chrono::system_clock::time_point> occurred_to;
    CursorPageRequest page;
};

struct CorrectTransferCommand {
    domain::TransferGroupId original_transfer_group_id;
    CreateTransferCommand replacement;
};

struct DeleteTransferCommand {
    domain::UserId user_id;
    domain::TransferGroupId transfer_group_id;
    std::optional<std::chrono::system_clock::time_point> deleted_at;
};

struct CategoryDto {
    domain::CategoryId id;
    std::string name;
    domain::CategoryBoard board = domain::CategoryBoard::Expense;
    domain::CategorySource source = domain::CategorySource::User;
    std::optional<domain::CategoryId> parent_id;
    std::optional<std::int64_t> template_id;
    int sort_order = 0;
    bool is_deleted = false;
    std::optional<std::chrono::system_clock::time_point> deleted_at;
    std::chrono::system_clock::time_point created_at{};
    std::chrono::system_clock::time_point updated_at{};
};

struct CategoryTreeDto : CategoryDto {
    std::vector<CategoryTreeDto> children;
};

struct CreateCategoryCommand {
    domain::UserId user_id;
    std::optional<domain::CategoryBoard> board;
    std::optional<std::string> name;
    std::optional<domain::CategoryId> parent_id;
    std::optional<std::int64_t> template_id;
};

struct DeleteCategoryCommand {
    domain::UserId user_id;
    domain::CategoryId category_id;
    std::optional<std::chrono::system_clock::time_point> deleted_at;
};

struct UpdateCategoryCommand {
    domain::UserId user_id;
    domain::CategoryId category_id;
    std::string name;
    int sort_order = 0;
};

struct RestoreCategoryCommand {
    domain::UserId user_id;
    domain::CategoryId category_id;
};

struct TagDto {
    domain::TagId id;
    std::string name;
    bool is_deleted = false;
    std::optional<std::chrono::system_clock::time_point> deleted_at;
    std::chrono::system_clock::time_point created_at{};
    std::chrono::system_clock::time_point updated_at{};
};

struct CreateTagCommand {
    domain::UserId user_id;
    std::string name;
};

struct DeleteTagCommand {
    domain::UserId user_id;
    domain::TagId tag_id;
    std::optional<std::chrono::system_clock::time_point> deleted_at;
};

struct UpdateTagCommand {
    domain::UserId user_id;
    domain::TagId tag_id;
    std::string name;
};

struct RestoreTagCommand {
    domain::UserId user_id;
    domain::TagId tag_id;
};

struct ReplaceTransactionTagsCommand {
    domain::UserId user_id;
    domain::TransactionId transaction_id;
    std::vector<domain::TagId> tag_ids;
};

struct UserPreferenceDto {
    std::string base_currency;
    std::string locale;
    std::string timezone;
    std::string date_format;
    std::string number_format;
    domain::ThemeMode theme = domain::ThemeMode::System;
    domain::HomePage default_home_page = domain::HomePage::Dashboard;
    domain::ReportPeriod default_report_period = domain::ReportPeriod::CurrentMonth;
};

struct UpdateUserPreferenceCommand {
    domain::UserId user_id;
    std::string base_currency;
    std::string locale;
    std::string timezone;
    std::string date_format;
    std::string number_format;
    domain::ThemeMode theme = domain::ThemeMode::System;
    domain::HomePage default_home_page = domain::HomePage::Dashboard;
    domain::ReportPeriod default_report_period = domain::ReportPeriod::CurrentMonth;
};

struct CurrencyMetadataDto {
    std::string code;
    std::string symbol;
    int precision = 2;
    std::string display_name;
    bool is_crypto = false;
};

struct CashFlowPeriodDto {
    std::string period;
    std::string income;
    std::string expense;
    std::string net;
};

struct CashFlowTrendDto {
    std::string base_currency;
    std::vector<CashFlowPeriodDto> trends;
};

struct CashFlowTrendQuery {
    domain::UserId user_id;
    int start_year = 0;
    unsigned start_month = 0;
    int end_year = 0;
    unsigned end_month = 0;
};

struct CashFlowDto {
    std::string currency_code;
    std::string income_total;
    std::string expense_total;
    std::string net_total;
};

struct NetWorthDto {
    std::string currency_code;
    // total == total_assets + total_liabilities (liabilities are negative).
    std::string total;
    std::string total_assets;      // sum of asset-category balances (>= 0 typically)
    std::string total_liabilities; // sum of liability-category balances (<= 0)
    std::chrono::system_clock::time_point generated_at{};
};

// One slice of the asset distribution / top-category breakdowns. `amount` and
// `percentage` are strings at the boundary; percentage is formatted like
// "68.0%" per the REST contract.
struct DistributionSliceDto {
    std::string label;
    std::string amount;
    std::string percentage;
};

struct CategoryBreakdownDto {
    std::optional<domain::CategoryId> category_id;
    std::string category_name;
    std::string amount;
    std::string percentage;
};

struct DashboardSummaryDto {
    std::string currency_code;
    std::string net_worth;
    std::string total_assets;
    std::string total_liabilities;
    std::string income_total;   // period income (default period = current month)
    std::string expense_total;  // period expense
    std::string cash_flow_net;
    std::size_t account_count = 0;
    std::vector<DistributionSliceDto> asset_distribution;
    std::vector<CategoryBreakdownDto> top_expense_categories;
    // Reporting window the income/expense/period stats were computed over.
    std::chrono::system_clock::time_point report_period_start{};
    std::chrono::system_clock::time_point report_period_end{};
    std::chrono::system_clock::time_point generated_at{};
};

enum class ReportDimension {
    RootCategory,
    Account,
    Tag
};

enum class ReportRateStatus {
    Current,
    Historical
};

struct ReportAnalysisQuery {
    domain::UserId user_id;
    int start_year = 0;
    unsigned start_month = 0;
    int end_year = 0;
    unsigned end_month = 0;
    ReportDimension dimension = ReportDimension::RootCategory;
};

struct NetWorthTrendPointDto {
    std::string period;
    std::string total_assets;
    std::string total_liabilities;
    std::string net_worth;
    std::chrono::system_clock::time_point valued_at{};
    ReportRateStatus rate_status = ReportRateStatus::Current;
};

struct ReportBreakdownSliceDto {
    std::string key;
    std::string label;
    std::string income;
    std::string expense;
    std::string net;
};

struct ReportAnalysisDto {
    std::string base_currency;
    std::chrono::system_clock::time_point valuation_at{};
    ReportRateStatus rate_status = ReportRateStatus::Current;
    std::chrono::system_clock::time_point report_period_start{};
    std::chrono::system_clock::time_point report_period_end{};
    ReportDimension dimension = ReportDimension::RootCategory;
    bool dimension_overlaps = false;
    std::vector<NetWorthTrendPointDto> net_worth_trend;
    std::vector<ReportBreakdownSliceDto> breakdown;
};

struct CsvExportDto {
    std::string filename;
    std::string content;
    std::size_t row_count = 0;
};

struct UserAuditLogQueryDto {
    domain::UserId user_id;
    std::optional<domain::AuditAction> action;
    std::optional<std::string> resource_type;
    std::optional<std::chrono::system_clock::time_point> from;
    std::optional<std::chrono::system_clock::time_point> to;
    std::optional<std::int64_t> before_id;
    std::size_t page_size = 50;
};

struct UserAuditLogItemDto {
    std::int64_t id = 0;
    domain::AuditAction action = domain::AuditAction::Create;
    std::string resource_type;
    std::string resource_id;
    std::string result = "success";
    std::optional<std::string> trace_id;
    std::chrono::system_clock::time_point occurred_at{};
};

struct UserAuditLogPageDto {
    std::vector<UserAuditLogItemDto> items;
    std::optional<std::string> next_cursor;
};

struct RebuildBalanceCacheCommand {
    domain::UserId user_id;
    std::optional<domain::AccountId> account_id;
    std::string trace_id;
};

struct BalanceCacheRebuildItemDto {
    domain::AccountId account_id;
    std::string currency_code;
    std::string balance;
    std::optional<domain::TransactionId> last_transaction_id;
    std::int64_t source_version = 0;
    std::int64_t cache_version = 1;
    std::chrono::system_clock::time_point rebuilt_at{};
};

struct BalanceCacheRebuildDto {
    std::vector<BalanceCacheRebuildItemDto> accounts;
};

struct RefreshExchangeRatesCommand {
    // Optional target currencies; empty => use active account currencies.
    std::vector<std::string> target_currency_codes;
};

struct RefreshExchangeRatesResultDto {
    std::size_t appended_count = 0;
    bool degraded = false;
    std::string message;
};

} // namespace pfh::application
