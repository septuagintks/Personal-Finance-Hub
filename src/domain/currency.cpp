// Personal Finance Hub - Currency Value Object Implementation
// Version: 1.0
// C++23

#include "pfh/domain/currency.h"
#include <algorithm>
#include <array>
#include <string_view>

namespace pfh::domain {

namespace {

// Controlled crypto whitelist (non ISO-4217). Codes here bypass the strict
// ISO-4217 fiat table but still require the 3-letter uppercase shape.
constexpr std::array<std::string_view, 3> kCryptoWhitelist = {
    "BTC", "ETH", "USDT"
};

// A pragmatic subset of ISO-4217 fiat codes relevant to the project. This is
// not the full ISO table; it covers the currencies the system supports today
// and can be extended as needed. Kept sorted for readability.
constexpr std::array<std::string_view, 20> kFiatCodes = {
    "AUD", "CAD", "CHF", "CNY", "EUR",
    "GBP", "HKD", "IDR", "INR", "JPY",
    "KRW", "MYR", "NZD", "PHP", "SGD",
    "THB", "TWD", "USD", "VND", "ZAR"
};

[[nodiscard]] bool is_in(const auto& table, std::string_view code) {
    return std::ranges::find(table, code) != table.end();
}

} // namespace

DomainResult<Currency> Currency::create(std::string_view code) {
    // Shape check: exactly 3 ASCII letters.
    if (code.size() != 3) {
        return std::unexpected(DomainError::invalid_currency(
            "code must be exactly 3 letters: '" + std::string(code) + "'"));
    }

    std::string upper;
    upper.reserve(3);
    for (const char c : code) {
        if (c >= 'a' && c <= 'z') {
            upper.push_back(static_cast<char>(c - 'a' + 'A'));
        } else if (c >= 'A' && c <= 'Z') {
            upper.push_back(c);
        } else {
            return std::unexpected(DomainError::invalid_currency(
                "code must contain only letters: '" + std::string(code) + "'"));
        }
    }

    if (!is_in(kFiatCodes, upper) && !is_in(kCryptoWhitelist, upper)) {
        return std::unexpected(DomainError::invalid_currency(
            "unsupported currency code: '" + upper + "'"));
    }

    return Currency(std::move(upper));
}

bool Currency::is_crypto() const noexcept {
    return is_in(kCryptoWhitelist, code_);
}

} // namespace pfh::domain
