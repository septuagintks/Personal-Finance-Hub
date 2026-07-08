// Personal Finance Hub - UserPreference Domain Object
// Version: 1.0
// C++23
//
// UserPreference is a domain concept that aggregates the user's base currency
// and extended preferences. The repository layer composes it from
// users.base_currency_code and the user_preferences table.

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/user.h"
#include <chrono>
#include <string>

namespace pfh::domain {

/// @brief Theme preference.
enum class ThemeMode {
    System,
    Light,
    Dark
};

/// @brief Default home page after login.
enum class HomePage {
    Dashboard,
    Transactions,
    Reports,
    Accounts
};

/// @brief Default time range for reports.
enum class ReportPeriod {
    CurrentMonth,
    LastMonth,
    Last3Months,
    CurrentYear,
    Custom
};

/// @brief User preferences domain object.
///
/// Combines the default base currency (from users.base_currency_code) and
/// extended preferences (from user_preferences table). Not a first-class entity;
/// it may be part of the User aggregate or managed independently depending on
/// use case orchestration.
class UserPreference {
public:
    UserPreference(
        UserId user_id,
        Currency base_currency,
        std::string locale = "en-US",
        std::string timezone = "UTC",
        std::string date_format = "YYYY-MM-DD",
        std::string number_format = "1,234.56",
        ThemeMode theme = ThemeMode::System,
        HomePage default_home_page = HomePage::Dashboard,
        ReportPeriod default_report_period = ReportPeriod::CurrentMonth)
        : user_id_(user_id),
          base_currency_(std::move(base_currency)),
          locale_(std::move(locale)),
          timezone_(std::move(timezone)),
          date_format_(std::move(date_format)),
          number_format_(std::move(number_format)),
          theme_(theme),
          default_home_page_(default_home_page),
          default_report_period_(default_report_period) {}

    [[nodiscard]] UserId user_id() const noexcept { return user_id_; }
    [[nodiscard]] const Currency& base_currency() const noexcept { return base_currency_; }
    [[nodiscard]] const std::string& locale() const noexcept { return locale_; }
    [[nodiscard]] const std::string& timezone() const noexcept { return timezone_; }
    [[nodiscard]] const std::string& date_format() const noexcept { return date_format_; }
    [[nodiscard]] const std::string& number_format() const noexcept { return number_format_; }
    [[nodiscard]] ThemeMode theme() const noexcept { return theme_; }
    [[nodiscard]] HomePage default_home_page() const noexcept { return default_home_page_; }
    [[nodiscard]] ReportPeriod default_report_period() const noexcept { return default_report_period_; }

private:
    UserId user_id_;
    Currency base_currency_;
    std::string locale_;
    std::string timezone_;
    std::string date_format_;
    std::string number_format_;
    ThemeMode theme_;
    HomePage default_home_page_;
    ReportPeriod default_report_period_;
};

} // namespace pfh::domain
