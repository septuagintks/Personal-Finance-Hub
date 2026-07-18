// Personal Finance Hub - PostgreSQL Row Mapping Helpers
// Version: 1.0
// C++23
//
// Shared helpers that translate between Drogon Result/Row and domain entities.
// All conversions go through Decimal/currency parsing so the NUMERIC columns ->
// string → Decimal pipeline never sees float representations.

#pragma once

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/domain/account.h"
#include "pfh/domain/category.h"
#include "pfh/domain/currency.h"
#include "pfh/domain/exchange_rate.h"
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/transaction.h"
#include "pfh/domain/user.h"
#include "pfh/domain/user_preference.h"

#include <drogon/orm/Field.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace pfh::infrastructure {

/// @brief PostgreSQL adapter helpers (no Drogon-specific types leak into repos).
namespace pg {

/// @brief Read a non-null TEXT/VARCHAR column by index.
[[nodiscard]] std::string getString(const drogon::orm::Row& row, size_t col);

/// @brief Read a nullable TEXT/VARCHAR column by index.
[[nodiscard]] std::optional<std::string> getOptionalString(
    const drogon::orm::Row& row, size_t col);

/// @brief Read a BIGINT primary key column.
[[nodiscard]] std::int64_t getBigInt(const drogon::orm::Row& row, size_t col);

/// @brief Read a nullable BIGINT column.
[[nodiscard]] std::optional<std::int64_t> getOptionalBigInt(
    const drogon::orm::Row& row, size_t col);

/// @brief Read a BOOLEAN column.
[[nodiscard]] bool getBool(const drogon::orm::Row& row, size_t col);

/// @brief Read a NUMERIC column as its string form (avoid double conversion).
[[nodiscard]] std::string getNumericAsString(
    const drogon::orm::Row& row, size_t col);

/// @brief Read a TIMESTAMPTZ column as system_clock::time_point.
[[nodiscard]] std::chrono::system_clock::time_point getTimestamp(
    const drogon::orm::Row& row, size_t col);

/// @brief Read a nullable TIMESTAMPTZ column.
[[nodiscard]] std::optional<std::chrono::system_clock::time_point> getOptionalTimestamp(
    const drogon::orm::Row& row, size_t col);

/// @brief Convert a domain timestamp to an explicit UTC PostgreSQL value.
[[nodiscard]] std::string toDbTimestamp(
    std::chrono::system_clock::time_point value);

[[nodiscard]] std::optional<std::string> toDbTimestamp(
    std::optional<std::chrono::system_clock::time_point> value);

/// @brief Map a PostgreSQL account_type enum text to domain::AccountType.
[[nodiscard]] domain::AccountType parseAccountType(const std::string& s);

/// @brief Map a domain::AccountType to its canonical SQL text form.
[[nodiscard]] std::string toSqlText(domain::AccountType type);

/// @brief Map a PostgreSQL account_category enum text to domain::AccountCategory.
[[nodiscard]] domain::AccountCategory parseAccountCategory(const std::string& s);

/// @brief Map a domain::AccountCategory to its canonical SQL text form.
[[nodiscard]] std::string toSqlText(domain::AccountCategory category);

/// @brief Map a PostgreSQL transaction_type enum text to domain::TransactionType.
[[nodiscard]] domain::TransactionType parseTransactionType(const std::string& s);

/// @brief Map a domain::TransactionType to its canonical SQL text form.
[[nodiscard]] std::string toSqlText(domain::TransactionType type);

/// @brief Map a PostgreSQL category_board enum text to domain::CategoryBoard.
[[nodiscard]] domain::CategoryBoard parseCategoryBoard(const std::string& s);

/// @brief Map a domain::CategoryBoard to its canonical SQL text form.
[[nodiscard]] std::string toSqlText(domain::CategoryBoard board);

/// @brief Map a PostgreSQL category_source enum text to domain::CategorySource.
[[nodiscard]] domain::CategorySource parseCategorySource(const std::string& s);

/// @brief Map a domain::CategorySource to its canonical SQL text form.
[[nodiscard]] std::string toSqlText(domain::CategorySource source);

/// @brief Map a PostgreSQL theme_mode enum text to domain::ThemeMode.
[[nodiscard]] domain::ThemeMode parseThemeMode(const std::string& s);

/// @brief Map a domain::ThemeMode to its canonical SQL text form.
[[nodiscard]] std::string toSqlText(domain::ThemeMode mode);

/// @brief Map a PostgreSQL default_home_page enum text to domain::HomePage.
[[nodiscard]] domain::HomePage parseDefaultHomePage(const std::string& s);

/// @brief Map a domain::HomePage to its canonical SQL text form.
[[nodiscard]] std::string toSqlText(domain::HomePage page);

/// @brief Map a PostgreSQL report_period enum text to domain::ReportPeriod.
[[nodiscard]] domain::ReportPeriod parseReportPeriod(const std::string& s);

/// @brief Map a domain::ReportPeriod to its canonical SQL text form.
[[nodiscard]] std::string toSqlText(domain::ReportPeriod period);

}  // namespace pg

}  // namespace pfh::infrastructure

#endif  // PFH_HAS_POSTGRESQL
