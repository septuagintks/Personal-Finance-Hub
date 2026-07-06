// Test Support - Common Test Utilities
// Version: 1.0
// Shared builders/helpers for domain unit tests. These unwrap DomainResult and
// fail the current test (via GoogleTest EXPECT) when construction is invalid,
// so tests can build valid fixtures in one line.

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/decimal.h"
#include "pfh/domain/exchange_rate.h"
#include "pfh/domain/money.h"
#include <gtest/gtest.h>
#include <chrono>
#include <ctime>
#include <string_view>

namespace pfh::test {

/// @brief Build a Decimal from a string, failing the test on parse error.
[[nodiscard]] inline domain::Decimal dec(std::string_view text) {
    auto r = domain::Decimal::parse(text);
    EXPECT_TRUE(r.has_value()) << "Failed to parse Decimal: " << text;
    return r.value_or(domain::Decimal{});
}

/// @brief Build a Currency from a code, failing the test on invalid code.
[[nodiscard]] inline domain::Currency ccy(std::string_view code) {
    auto r = domain::Currency::create(code);
    EXPECT_TRUE(r.has_value()) << "Failed to create Currency: " << code;
    // Fall back to USD (always valid) so a failed EXPECT still yields a usable object.
    return r.value_or(domain::Currency::create("USD").value());
}

/// @brief Build Money from an amount string and currency code.
[[nodiscard]] inline domain::Money money(std::string_view amount, std::string_view code) {
    return domain::Money(dec(amount), ccy(code));
}

/// @brief A fixed sample time point (2024-06-25 08:00:00 UTC).
[[nodiscard]] inline domain::ExchangeRate::TimePoint sample_time() {
    return std::chrono::system_clock::from_time_t(1719302400);
}

/// @brief Build a TimePoint from an epoch second value.
[[nodiscard]] inline domain::ExchangeRate::TimePoint time_at(std::time_t seconds) {
    return std::chrono::system_clock::from_time_t(seconds);
}

/// @brief Build an ExchangeRate, failing the test on invalid input.
[[nodiscard]] inline domain::ExchangeRate rate(
    std::string_view base, std::string_view target, std::string_view r,
    std::time_t when = 1719302400, std::string source = "Test") {
    auto er = domain::ExchangeRate::create(ccy(base), ccy(target), dec(r),
                                           time_at(when), std::move(source));
    EXPECT_TRUE(er.has_value())
        << "Failed to create ExchangeRate: " << base << "->" << target << " @ " << r;
    return er.value_or(
        domain::ExchangeRate::create(ccy("USD"), ccy("CNY"), dec("1"),
                                     time_at(when), "fallback").value());
}

} // namespace pfh::test
