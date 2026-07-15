// Test Support - Common Test Utilities
// Version: 1.0
// Shared builders/helpers for domain unit tests. These unwrap DomainResult and
// fail the current test (via GoogleTest EXPECT) when construction is invalid,
// so tests can build valid fixtures in one line.

#pragma once

#include "pfh/application/ports/i_request_hasher.h"
#include "pfh/domain/currency.h"
#include "pfh/domain/decimal.h"
#include "pfh/domain/exchange_rate.h"
#include "pfh/domain/money.h"
#include <gtest/gtest.h>
#include <chrono>
#include <array>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace pfh::test {

class DeterministicRequestHasher final
    : public application::IRequestHasher {
public:
    [[nodiscard]] application::Result<std::string> sha256(
        std::string_view value) const override {
        std::array<std::uint64_t, 4> state{
            14695981039346656037ULL,
            1099511628211ULL,
            7809847782465536322ULL,
            9650029242287828579ULL};
        for (const auto raw : value) {
            const auto byte = static_cast<unsigned char>(raw);
            for (std::size_t index = 0; index < state.size(); ++index) {
                state[index] ^= static_cast<std::uint64_t>(byte) + index;
                state[index] *= 1099511628211ULL + index * 2ULL;
            }
        }
        static constexpr char kHex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (const auto word : state) {
            for (int shift = 60; shift >= 0; shift -= 4) {
                result.push_back(kHex[(word >> shift) & 0x0fULL]);
            }
        }
        return result;
    }
};

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
