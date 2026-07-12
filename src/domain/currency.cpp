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
// 3-5 uppercase letters, so they are NOT subject to the strict 3-letter
// ISO-4217 shape rule; they only need to appear here.
//
// IMPORTANT: this list MUST stay in sync with the currencies seeded by
// migrations/V2__seed_initial_currencies.sql. Domain and database share one
// supported-currency set; a code accepted here but absent from the DB (or vice
// versa) would let an account be created that cannot be persisted, or a stored
// currency the domain rejects on read. When adding a currency, update BOTH.
constexpr std::array<std::string_view, 13> kCryptoWhitelist = {
    "ADA", "BNB", "BTC", "DOGE", "DOT",
    "ETH", "MATIC", "SOL", "TRX", "USDC",
    "USDT", "WBTC", "XRP"
};

// Bounds for a crypto ticker's letter count.
constexpr std::size_t kCryptoMinLen = 3;
constexpr std::size_t kCryptoMaxLen = 5;

// A pragmatic subset of ISO-4217 fiat codes relevant to the project. This is
// not the full ISO table; it is the exact set seeded by V2 (see the sync note
// above). Kept sorted for readability.
constexpr std::array<std::string_view, 20> kFiatCodes = {
    "AUD", "BRL", "CAD", "CHF", "CNY",
    "EUR", "GBP", "HKD", "INR", "JPY",
    "KRW", "MXN", "NOK", "NZD", "RUB",
    "SEK", "SGD", "TWD", "USD", "ZAR"
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
