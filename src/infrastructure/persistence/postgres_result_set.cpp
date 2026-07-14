// Personal Finance Hub - PostgreSQL Row Mapping Helpers Implementation
// Version: 1.0
// C++23

#include "pfh/infrastructure/persistence/postgres_result_set.h"

#ifdef PFH_HAS_POSTGRESQL

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pfh::infrastructure::pg {

namespace {

[[nodiscard]] int parseNumber(
    std::string_view value, size_t offset, size_t length) {
    if (offset > value.size() || length > value.size() - offset) {
        throw std::invalid_argument("timestamp component is missing");
    }

    int parsed = 0;
    const auto* first = value.data() + offset;
    const auto* last = first + length;
    const auto [end, error] = std::from_chars(first, last, parsed);
    if (error != std::errc{} || end != last) {
        throw std::invalid_argument("timestamp component is invalid");
    }
    return parsed;
}

[[nodiscard]] std::chrono::seconds parseUtcOffset(
    std::string_view value) {
    if (value == "Z") {
        return std::chrono::seconds::zero();
    }
    if (value.size() != 3 && value.size() != 6 && value.size() != 9) {
        throw std::invalid_argument("timestamp UTC offset has invalid length");
    }
    if ((value.front() != '+' && value.front() != '-') ||
        (value.size() >= 6 && value[3] != ':') ||
        (value.size() == 9 && value[6] != ':')) {
        throw std::invalid_argument("timestamp UTC offset is invalid");
    }

    const int hours = parseNumber(value, 1, 2);
    const int minutes = value.size() >= 6 ? parseNumber(value, 4, 2) : 0;
    const int seconds = value.size() == 9 ? parseNumber(value, 7, 2) : 0;
    if (hours > 15 || minutes > 59 || seconds > 59) {
        throw std::invalid_argument("timestamp UTC offset is out of range");
    }

    const auto magnitude = std::chrono::hours(hours) +
                           std::chrono::minutes(minutes) +
                           std::chrono::seconds(seconds);
    return value.front() == '+' ? magnitude : -magnitude;
}

[[nodiscard]] std::chrono::system_clock::time_point parseTimestamp(
    std::string_view value) {
    size_t offset_position = std::string_view::npos;
    if (!value.empty() && value.back() == 'Z') {
        offset_position = value.size() - 1;
    } else {
        offset_position = value.find_last_of("+-");
    }
    if (offset_position == std::string_view::npos || offset_position < 19) {
        throw std::invalid_argument("timestamp UTC offset is missing");
    }

    const auto timestamp = value.substr(0, offset_position);
    const auto utc_offset = parseUtcOffset(value.substr(offset_position));
    if (timestamp.size() < 19 || timestamp.size() > 26 ||
        timestamp[4] != '-' || timestamp[7] != '-' ||
        timestamp[10] != ' ' || timestamp[13] != ':' ||
        timestamp[16] != ':') {
        throw std::invalid_argument("timestamp layout is invalid");
    }

    const int year = parseNumber(timestamp, 0, 4);
    const int month = parseNumber(timestamp, 5, 2);
    const int day = parseNumber(timestamp, 8, 2);
    const int hour = parseNumber(timestamp, 11, 2);
    const int minute = parseNumber(timestamp, 14, 2);
    const int second = parseNumber(timestamp, 17, 2);

    int microseconds = 0;
    if (timestamp.size() > 19) {
        const size_t fraction_length = timestamp.size() - 20;
        if (timestamp[19] != '.' || fraction_length == 0 ||
            fraction_length > 6) {
            throw std::invalid_argument("timestamp fraction is invalid");
        }
        microseconds = parseNumber(timestamp, 20, fraction_length);
        for (size_t digit = fraction_length; digit < 6; ++digit) {
            microseconds *= 10;
        }
    }

    const std::chrono::year_month_day date{
        std::chrono::year(year),
        std::chrono::month(static_cast<unsigned>(month)),
        std::chrono::day(static_cast<unsigned>(day))};
    if (!date.ok() || hour > 23 || minute > 59 || second > 59) {
        throw std::invalid_argument("timestamp value is out of range");
    }

    const auto local_time = std::chrono::sys_days(date) +
                            std::chrono::hours(hour) +
                            std::chrono::minutes(minute) +
                            std::chrono::seconds(second) +
                            std::chrono::microseconds(microseconds);
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        local_time - utc_offset);
}

}  // namespace

std::string getString(const drogon::orm::Row& row, size_t col) {
    if (row[col].isNull()) {
        throw std::runtime_error("pg::getString: column " + std::to_string(col) + " is NULL");
    }
    return row[col].as<std::string>();
}

std::optional<std::string> getOptionalString(const drogon::orm::Row& row, size_t col) {
    if (row[col].isNull()) {
        return std::nullopt;
    }
    return row[col].as<std::string>();
}

std::int64_t getBigInt(const drogon::orm::Row& row, size_t col) {
    if (row[col].isNull()) {
        throw std::runtime_error("pg::getBigInt: column " + std::to_string(col) + " is NULL");
    }
    return row[col].as<std::int64_t>();
}

std::optional<std::int64_t> getOptionalBigInt(const drogon::orm::Row& row, size_t col) {
    if (row[col].isNull()) {
        return std::nullopt;
    }
    return row[col].as<std::int64_t>();
}

bool getBool(const drogon::orm::Row& row, size_t col) {
    if (row[col].isNull()) {
        throw std::runtime_error("pg::getBool: column " + std::to_string(col) + " is NULL");
    }
    return row[col].as<bool>();
}

std::string getNumericAsString(const drogon::orm::Row& row, size_t col) {
    if (row[col].isNull()) {
        throw std::runtime_error("pg::getNumericAsString: column " + std::to_string(col) + " is NULL");
    }
    // Drogon ORM can read NUMERIC directly as string to avoid double precision loss.
    return row[col].as<std::string>();
}

std::chrono::system_clock::time_point getTimestamp(const drogon::orm::Row& row, size_t col) {
    if (row[col].isNull()) {
        throw std::runtime_error("pg::getTimestamp: column " + std::to_string(col) + " is NULL");
    }
    try {
        return parseTimestamp(row[col].as<std::string>());
    } catch (const std::invalid_argument&) {
        throw std::runtime_error(
            "pg::getTimestamp: column " + std::to_string(col) +
            " is not a valid TIMESTAMPTZ value");
    }
}

std::optional<std::chrono::system_clock::time_point> getOptionalTimestamp(
    const drogon::orm::Row& row, size_t col) {
    if (row[col].isNull()) {
        return std::nullopt;
    }
    return getTimestamp(row, col);
}

trantor::Date toDbTimestamp(std::chrono::system_clock::time_point value) {
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        value.time_since_epoch());
    return trantor::Date(micros.count());
}

std::optional<trantor::Date> toDbTimestamp(
    std::optional<std::chrono::system_clock::time_point> value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return toDbTimestamp(*value);
}

// ============================================================================
// Enum mapping: PostgreSQL TEXT <-> Domain C++ enum
// ============================================================================

domain::AccountType parseAccountType(const std::string& s) {
    if (s == "cash") return domain::AccountType::Cash;
    if (s == "savings") return domain::AccountType::Savings;
    if (s == "credit") return domain::AccountType::Credit;
    if (s == "digital_wallet") return domain::AccountType::DigitalWallet;
    if (s == "investment") return domain::AccountType::Investment;
    if (s == "crypto") return domain::AccountType::Crypto;
    if (s == "other") return domain::AccountType::Other;
    throw std::invalid_argument("Unknown account_type: " + s);
}

std::string toSqlText(domain::AccountType type) {
    switch (type) {
        case domain::AccountType::Cash: return "cash";
        case domain::AccountType::Savings: return "savings";
        case domain::AccountType::Credit: return "credit";
        case domain::AccountType::DigitalWallet: return "digital_wallet";
        case domain::AccountType::Investment: return "investment";
        case domain::AccountType::Crypto: return "crypto";
        case domain::AccountType::Other: return "other";
    }
    throw std::invalid_argument("Invalid AccountType enum value");
}

domain::AccountCategory parseAccountCategory(const std::string& s) {
    if (s == "asset") return domain::AccountCategory::Asset;
    if (s == "liability") return domain::AccountCategory::Liability;
    throw std::invalid_argument("Unknown account_category: " + s);
}

std::string toSqlText(domain::AccountCategory category) {
    switch (category) {
        case domain::AccountCategory::Asset: return "asset";
        case domain::AccountCategory::Liability: return "liability";
    }
    throw std::invalid_argument("Invalid AccountCategory enum value");
}

domain::TransactionType parseTransactionType(const std::string& s) {
    if (s == "income") return domain::TransactionType::Income;
    if (s == "expense") return domain::TransactionType::Expense;
    if (s == "transfer") return domain::TransactionType::Transfer;
    if (s == "adjustment") return domain::TransactionType::Adjustment;
    throw std::invalid_argument("Unknown transaction_type: " + s);
}

std::string toSqlText(domain::TransactionType type) {
    switch (type) {
        case domain::TransactionType::Income: return "income";
        case domain::TransactionType::Expense: return "expense";
        case domain::TransactionType::Transfer: return "transfer";
        case domain::TransactionType::Adjustment: return "adjustment";
    }
    throw std::invalid_argument("Invalid TransactionType enum value");
}

domain::CategoryBoard parseCategoryBoard(const std::string& s) {
    if (s == "income") return domain::CategoryBoard::Income;
    if (s == "expense") return domain::CategoryBoard::Expense;
    throw std::invalid_argument("Unknown category_board: " + s);
}

std::string toSqlText(domain::CategoryBoard board) {
    switch (board) {
        case domain::CategoryBoard::Income: return "income";
        case domain::CategoryBoard::Expense: return "expense";
    }
    throw std::invalid_argument("Invalid CategoryBoard enum value");
}

domain::CategorySource parseCategorySource(const std::string& s) {
    if (s == "system") return domain::CategorySource::System;
    if (s == "user") return domain::CategorySource::User;
    throw std::invalid_argument("Unknown category_source: " + s);
}

std::string toSqlText(domain::CategorySource source) {
    switch (source) {
        case domain::CategorySource::System: return "system";
        case domain::CategorySource::User: return "user";
    }
    throw std::invalid_argument("Invalid CategorySource enum value");
}

domain::ThemeMode parseThemeMode(const std::string& s) {
    if (s == "system") return domain::ThemeMode::System;
    if (s == "light") return domain::ThemeMode::Light;
    if (s == "dark") return domain::ThemeMode::Dark;
    throw std::invalid_argument("Unknown theme_mode: " + s);
}

std::string toSqlText(domain::ThemeMode mode) {
    switch (mode) {
        case domain::ThemeMode::System: return "system";
        case domain::ThemeMode::Light: return "light";
        case domain::ThemeMode::Dark: return "dark";
    }
    throw std::invalid_argument("Invalid ThemeMode enum value");
}

domain::HomePage parseDefaultHomePage(const std::string& s) {
    if (s == "dashboard") return domain::HomePage::Dashboard;
    if (s == "transactions") return domain::HomePage::Transactions;
    if (s == "reports") return domain::HomePage::Reports;
    if (s == "accounts") return domain::HomePage::Accounts;
    throw std::invalid_argument("Unknown default_home_page: " + s);
}

std::string toSqlText(domain::HomePage page) {
    switch (page) {
        case domain::HomePage::Dashboard: return "dashboard";
        case domain::HomePage::Transactions: return "transactions";
        case domain::HomePage::Reports: return "reports";
        case domain::HomePage::Accounts: return "accounts";
    }
    throw std::invalid_argument("Invalid HomePage enum value");
}

domain::ReportPeriod parseReportPeriod(const std::string& s) {
    if (s == "current_month") return domain::ReportPeriod::CurrentMonth;
    if (s == "last_month") return domain::ReportPeriod::LastMonth;
    if (s == "last_3_months") return domain::ReportPeriod::Last3Months;
    if (s == "current_year") return domain::ReportPeriod::CurrentYear;
    if (s == "custom") return domain::ReportPeriod::Custom;
    throw std::invalid_argument("Unknown report_period: " + s);
}

std::string toSqlText(domain::ReportPeriod period) {
    switch (period) {
        case domain::ReportPeriod::CurrentMonth: return "current_month";
        case domain::ReportPeriod::LastMonth: return "last_month";
        case domain::ReportPeriod::Last3Months: return "last_3_months";
        case domain::ReportPeriod::CurrentYear: return "current_year";
        case domain::ReportPeriod::Custom: return "custom";
    }
    throw std::invalid_argument("Invalid ReportPeriod enum value");
}

}  // namespace pfh::infrastructure::pg

#endif  // PFH_HAS_POSTGRESQL
