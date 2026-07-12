// Personal Finance Hub - Application DTOs
// Version: 1.0
// C++23
//
// API-facing DTOs. Amounts are always strings at the application boundary.

#pragma once

#include "pfh/domain/account.h"
#include "pfh/domain/category.h"
#include "pfh/domain/money.h"
#include "pfh/domain/transaction.h"
#include "pfh/domain/user.h"
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
    std::int64_t version = 1;
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
};

struct CreateTransactionCommand {
    domain::UserId user_id;
    domain::AccountId account_id;
    domain::TransactionType type = domain::TransactionType::Expense;
    std::string amount; // string input
    std::string currency_code;
    std::string description;
    std::optional<domain::CategoryId> category_id;
    // When category_id is provided, category_board must also be provided so the
    // use case can enforce the board rule (Income->income, Expense/Adjustment->
    // expense). The presentation/persistence layer resolves the board from the
    // category; until ICategoryRepository exists this is passed in explicitly.
    std::optional<domain::CategoryBoard> category_board;
    // Business time. nullopt => the use case stamps the current time. Never
    // default to epoch 0 (1970), which would corrupt current-month reports and
    // point-in-time historical rate selection.
    std::optional<std::chrono::system_clock::time_point> occurred_at;
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
    std::string description;
    // nullopt => the use case stamps the current time. Never epoch 0.
    std::optional<std::chrono::system_clock::time_point> occurred_at;
};

struct TransferResultDto {
    domain::TransferGroupId transfer_group_id;
    domain::TransactionId outgoing_transaction_id;
    domain::TransactionId incoming_transaction_id;
    std::string outgoing_amount;
    std::string incoming_amount;
    std::optional<std::string> rate;
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
