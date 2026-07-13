// Personal Finance Hub - Currency Value Object Implementation
// Version: 1.0
// C++23

#include "pfh/domain/currency.h"
#include <algorithm>
#include <array>
#include <string_view>

namespace pfh::domain {

namespace {

// Keep this catalog in the same order and with the same values as V2. It is the
// single Domain source for both validation and the public metadata endpoint.
constexpr std::array<CurrencyMetadata, 33> kCurrencyCatalog = {{
    {"USD", "$", "US Dollar", 2, false},
    {"CNY", "\xC2\xA5", "Chinese Yuan", 2, false},
    {"EUR", "\xE2\x82\xAC", "Euro", 2, false},
    {"GBP", "\xC2\xA3", "British Pound", 2, false},
    {"JPY", "\xC2\xA5", "Japanese Yen", 0, false},
    {"HKD", "HK$", "Hong Kong Dollar", 2, false},
    {"AUD", "A$", "Australian Dollar", 2, false},
    {"CAD", "C$", "Canadian Dollar", 2, false},
    {"CHF", "CHF", "Swiss Franc", 2, false},
    {"SGD", "S$", "Singapore Dollar", 2, false},
    {"KRW", "\xE2\x82\xA9", "South Korean Won", 0, false},
    {"INR", "\xE2\x82\xB9", "Indian Rupee", 2, false},
    {"RUB", "\xE2\x82\xBD", "Russian Ruble", 2, false},
    {"BRL", "R$", "Brazilian Real", 2, false},
    {"ZAR", "R", "South African Rand", 2, false},
    {"MXN", "Mex$", "Mexican Peso", 2, false},
    {"NZD", "NZ$", "New Zealand Dollar", 2, false},
    {"SEK", "kr", "Swedish Krona", 2, false},
    {"NOK", "kr", "Norwegian Krone", 2, false},
    {"TWD", "NT$", "Taiwan Dollar", 2, false},
    {"BTC", "\xE2\x82\xBF", "Bitcoin", 8, true},
    {"ETH", "\xCE\x9E", "Ethereum", 8, true},
    {"USDT", "USDT", "Tether", 8, true},
    {"USDC", "USDC", "USD Coin", 8, true},
    {"BNB", "BNB", "Binance Coin", 8, true},
    {"XRP", "XRP", "Ripple", 6, true},
    {"ADA", "ADA", "Cardano", 6, true},
    {"DOGE", "\xC3\x90", "Dogecoin", 8, true},
    {"SOL", "SOL", "Solana", 8, true},
    {"TRX", "TRX", "TRON", 6, true},
    {"MATIC", "MATIC", "Polygon", 8, true},
    {"DOT", "DOT", "Polkadot", 8, true},
    {"WBTC", "WBTC", "Wrapped Bitcoin", 8, true}
}};

// Bounds for a crypto ticker's letter count.
constexpr std::size_t kCryptoMinLen = 3;
constexpr std::size_t kCryptoMaxLen = 5;

[[nodiscard]] const CurrencyMetadata* find_metadata(std::string_view code) {
    const auto found = std::ranges::find_if(
        kCurrencyCatalog,
        [code](const CurrencyMetadata& metadata) { return metadata.code == code; });
    return found == kCurrencyCatalog.end() ? nullptr : &*found;
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

    if (find_metadata(upper) == nullptr) {
        return std::unexpected(DomainError::invalid_currency(
            "unsupported currency code: '" + upper + "'"));
    }

    return Currency(std::move(upper));
}

bool Currency::is_crypto() const noexcept {
    const auto* metadata = find_metadata(code_);
    return metadata != nullptr && metadata->is_crypto;
}

std::span<const CurrencyMetadata> Currency::catalog() noexcept {
    return kCurrencyCatalog;
}

} // namespace pfh::domain
