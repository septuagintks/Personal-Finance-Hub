// Personal Finance Hub - Currency Value Object Implementation
// Version: 1.0
// C++23

#include "pfh/domain/currency.h"
#include <algorithm>
#include <array>
#include <string_view>

namespace pfh::domain {

namespace {

// Controlled crypto whitelist (non ISO-4217). Crypto tickers are commonly
// 3-5 uppercase letters (BTC, ETH, USDT, USDC, WBTC), so they are NOT subject
// to the strict 3-letter ISO-4217 shape rule; they only need to appear here.
constexpr std::array<std::string_view, 5> kCryptoWhitelist = {
    "BTC", "ETH", "USDT", "USDC", "WBTC"
};

// Bounds for a crypto ticker's letter count.
constexpr std::size_t kCryptoMinLen = 3;
constexpr std::size_t kCryptoMaxLen = 5;

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
    // Shape check: 3-5 ASCII letters. Fiat (ISO-4217) is exactly 3; crypto
    // tickers may be up to 5. The final fiat/crypto membership check below
    // enforces the exact-3 rule for fiat implicitly (fiat table holds only
    // 3-letter codes), so we accept the wider range here.
    if (code.size() < kCryptoMinLen || code.size() > kCryptoMaxLen) {
        return std::unexpected(DomainError::invalid_currency(
            "code must be 3-5 letters: '" + std::string(code) + "'"));
    }

    std::string upper;
    upper.reserve(code.size());
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

    // A 3-letter code may be fiat or crypto; a 4-5 letter code must be crypto.
    const bool is_fiat = (upper.size() == 3) && is_in(kFiatCodes, upper);
    const bool is_crypto = is_in(kCryptoWhitelist, upper);
    if (!is_fiat && !is_crypto) {
        return std::unexpected(DomainError::invalid_currency(
            "unsupported currency code: '" + upper + "'"));
    }

    return Currency(std::move(upper));
}

bool Currency::is_crypto() const noexcept {
    return is_in(kCryptoWhitelist, code_);
}

} // namespace pfh::domain
