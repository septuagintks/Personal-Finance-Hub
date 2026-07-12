// Personal Finance Hub - Decimal Fixed-Point Type Implementation
// Version: 1.0
// C++23

#include "pfh/domain/decimal.h"
#include <array>
#include <charconv>
#include <cstdlib>
#include <limits>

// The Decimal storage type is GCC/Clang's native __int128. Under -pedantic the
// compiler warns that ISO C++ does not support __int128; that is expected and
// deliberate (see the P1-S05 decision to use native __int128_t). Suppress the
// pedantic diagnostic for this translation unit only.
#if defined(__clang__)
#    pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#    pragma GCC diagnostic ignored "-Wpedantic"
#endif

namespace pfh::domain {

namespace {

using Storage = Decimal::StorageType;
using UStorage = Decimal::UStorageType;

// Maximum absolute value we treat as valid. __int128 max is ~1.7e38.
// With scale 10^10 that leaves ~1.7e28 for the integer part, far beyond the
// NUMERIC(20,8) requirement. We keep the full range and only guard true overflow.
constexpr Storage kInt128Max = (static_cast<Storage>(1) << 126); // safe headroom bound

/// @brief Multiply two 128-bit values, detecting overflow.
///
/// Signed overflow is undefined behaviour, so the product is computed in the
/// unsigned domain (well-defined wraparound) and range-checked before casting
/// back. We reject any product whose magnitude exceeds the signed max so the
/// signed result is always representable.
[[nodiscard]] bool checked_mul(Storage a, Storage b, Storage& out) noexcept {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    const bool negative = (a < 0) != (b < 0);
    const UStorage ua = (a < 0) ? static_cast<UStorage>(-a) : static_cast<UStorage>(a);
    const UStorage ub = (b < 0) ? static_cast<UStorage>(-b) : static_cast<UStorage>(b);

    const UStorage product = ua * ub;
    // Overflow of the unsigned product itself.
    if (product / ub != ua) {
        return false;
    }
    // Ensure the magnitude fits in the signed range (|min| == max + 1).
    const UStorage signed_max = static_cast<UStorage>(std::numeric_limits<Storage>::max());
    if (negative) {
        if (product == signed_max + 1) {
            // Exactly INT_MIN: representable, but negating the cast would
            // overflow, so produce it directly.
            out = std::numeric_limits<Storage>::min();
            return true;
        }
        if (product > signed_max) {
            return false;
        }
        out = -static_cast<Storage>(product);
    } else {
        if (product > signed_max) {
            return false;
        }
        out = static_cast<Storage>(product);
    }
    return true;
}

/// @brief Add two 128-bit values, detecting overflow.
[[nodiscard]] bool checked_add(Storage a, Storage b, Storage& out) noexcept {
    // Overflow if both same sign and result flips sign.
    Storage result = static_cast<Storage>(static_cast<UStorage>(a) +
                                          static_cast<UStorage>(b));
    if (((a ^ result) & (b ^ result)) < 0) {
        return false;
    }
    out = result;
    return true;
}

/// @brief Convert an unsigned 128-bit magnitude to a decimal digit string.
[[nodiscard]] std::string u128_to_string(UStorage v) {
    if (v == 0) {
        return "0";
    }
    std::array<char, 40> buf{};
    std::size_t pos = buf.size();
    while (v > 0) {
        buf[--pos] = static_cast<char>('0' + static_cast<int>(v % 10));
        v /= 10;
    }
    return std::string(buf.data() + pos, buf.size() - pos);
}

} // namespace

DomainResult<Decimal> Decimal::from_integer(std::int64_t value) {
    Storage scaled = 0;
    if (!checked_mul(static_cast<Storage>(value), kScaleFactor, scaled)) {
        return std::unexpected(DomainError::overflow("from_integer"));
    }
    return Decimal(scaled);
}

namespace {

[[nodiscard]] std::size_t significant_fractional_digits(
    std::string_view text) noexcept {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }

    const auto dot = text.substr(begin, end - begin).find('.');
    if (dot == std::string_view::npos) {
        return 0;
    }
    const std::size_t absolute_dot = begin + dot;
    std::size_t fractional_end = end;
    while (fractional_end > absolute_dot + 1 && text[fractional_end - 1] == '0') {
        --fractional_end;
    }
    return fractional_end - (absolute_dot + 1);
}

} // namespace

DomainResult<Decimal> Decimal::parse_numeric_20_8(std::string_view text) {
    auto parsed = parse(text);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (significant_fractional_digits(text) > 8) {
        return std::unexpected(DomainError::invalid_amount(
            "more than 8 fractional digits for NUMERIC(20,8)"));
    }
    if (!parsed->fits_numeric_20_8()) {
        return std::unexpected(DomainError::invalid_amount(
            "outside NUMERIC(20,8) range"));
    }
    return parsed;
}

DomainResult<Decimal> Decimal::parse_numeric_20_10(std::string_view text) {
    auto parsed = parse(text);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (significant_fractional_digits(text) > 10) {
        return std::unexpected(DomainError::invalid_exchange_rate(
            "more than 10 fractional digits for NUMERIC(20,10)"));
    }
    if (!parsed->fits_numeric_20_10()) {
        return std::unexpected(DomainError::invalid_exchange_rate(
            "outside NUMERIC(20,10) range"));
    }
    return parsed;
}

DomainResult<Decimal> Decimal::parse(std::string_view text) {
    // Trim surrounding whitespace.
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    text = text.substr(begin, end - begin);

    if (text.empty()) {
        return std::unexpected(DomainError::parse_error("empty string"));
    }

    // Sign.
    bool negative = false;
    std::size_t i = 0;
    if (text[i] == '+' || text[i] == '-') {
        negative = (text[i] == '-');
        ++i;
    }
    if (i >= text.size()) {
        return std::unexpected(DomainError::parse_error("no digits"));
    }

    // Split into integer and fractional digit runs.
    std::string int_part;
    std::string frac_part;
    bool seen_dot = false;
    bool seen_digit = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '.') {
            if (seen_dot) {
                return std::unexpected(DomainError::parse_error("multiple decimal points"));
            }
            seen_dot = true;
        } else if (c >= '0' && c <= '9') {
            seen_digit = true;
            if (seen_dot) {
                frac_part.push_back(c);
            } else {
                int_part.push_back(c);
            }
        } else {
            return std::unexpected(DomainError::parse_error(
                std::string("invalid character '") + c + "'"));
        }
    }
    if (!seen_digit) {
        return std::unexpected(DomainError::parse_error("no digits"));
    }

    // Round fractional part to kScale digits using Half-Even.
    bool round_up = false;
    if (frac_part.size() > static_cast<std::size_t>(kScale)) {
        const char first_dropped = frac_part[static_cast<std::size_t>(kScale)];
        // Determine if anything nonzero exists beyond the first dropped digit.
        bool rest_nonzero = false;
        for (std::size_t k = static_cast<std::size_t>(kScale) + 1; k < frac_part.size(); ++k) {
            if (frac_part[k] != '0') {
                rest_nonzero = true;
                break;
            }
        }
        if (first_dropped > '5' || (first_dropped == '5' && rest_nonzero)) {
            round_up = true;
        } else if (first_dropped == '5' && !rest_nonzero) {
            // Exactly halfway: round to even.
            const char last_kept = (kScale > 0)
                ? frac_part[static_cast<std::size_t>(kScale) - 1]
                : (int_part.empty() ? '0' : int_part.back());
            if (((last_kept - '0') % 2) != 0) {
                round_up = true;
            }
        }
        frac_part.resize(static_cast<std::size_t>(kScale));
    } else {
        // Pad with zeros up to kScale.
        frac_part.append(static_cast<std::size_t>(kScale) - frac_part.size(), '0');
    }

    // Build magnitude = int_part * 10^kScale + frac_part, with overflow checks.
    Storage magnitude = 0;
    auto push_digit = [&](char digit) -> bool {
        Storage tmp = 0;
        if (!checked_mul(magnitude, static_cast<Storage>(10), tmp)) {
            return false;
        }
        if (!checked_add(tmp, static_cast<Storage>(digit - '0'), magnitude)) {
            return false;
        }
        return true;
    };

    for (const char c : int_part) {
        if (!push_digit(c)) {
            return std::unexpected(DomainError::overflow("integer part too large"));
        }
    }
    for (const char c : frac_part) {
        if (!push_digit(c)) {
            return std::unexpected(DomainError::overflow("value too large"));
        }
    }

    // Apply Half-Even rounding carry.
    if (round_up) {
        Storage carried = 0;
        if (!checked_add(magnitude, static_cast<Storage>(1), carried)) {
            return std::unexpected(DomainError::overflow("rounding carry"));
        }
        magnitude = carried;
    }

    if (magnitude > kInt128Max) {
        return std::unexpected(DomainError::overflow("value out of range"));
    }

    return Decimal(negative ? -magnitude : magnitude);
}

std::string Decimal::to_string() const {
    if (value_ == 0) {
        return "0";
    }

    const bool negative = value_ < 0;
    UStorage magnitude = negative
        ? static_cast<UStorage>(-value_)
        : static_cast<UStorage>(value_);

    const UStorage scale = static_cast<UStorage>(kScaleFactor);
    const UStorage int_part = magnitude / scale;
    const UStorage frac_part = magnitude % scale;

    std::string result;
    if (negative) {
        result.push_back('-');
    }
    result += u128_to_string(int_part);

    if (frac_part != 0) {
        // Zero-pad fractional to kScale, then trim trailing zeros.
        std::string frac = u128_to_string(frac_part);
        if (frac.size() < static_cast<std::size_t>(kScale)) {
            frac.insert(frac.begin(),
                        static_cast<std::size_t>(kScale) - frac.size(), '0');
        }
        while (!frac.empty() && frac.back() == '0') {
            frac.pop_back();
        }
        if (!frac.empty()) {
            result.push_back('.');
            result += frac;
        }
    }

    return result;
}

DomainResult<Decimal> Decimal::add(const Decimal& other) const {
    Storage result = 0;
    if (!checked_add(value_, other.value_, result)) {
        return std::unexpected(DomainError::overflow("add"));
    }
    return Decimal(result);
}

DomainResult<Decimal> Decimal::subtract(const Decimal& other) const {
    Storage negated_other = 0;
    // -other cannot overflow for our valid range, but guard the extreme.
    if (other.value_ == std::numeric_limits<Storage>::min()) {
        return std::unexpected(DomainError::overflow("subtract"));
    }
    negated_other = -other.value_;
    Storage result = 0;
    if (!checked_add(value_, negated_other, result)) {
        return std::unexpected(DomainError::overflow("subtract"));
    }
    return Decimal(result);
}

DomainResult<Decimal> Decimal::multiply(const Decimal& other) const {
    // (a * 10^s) * (b * 10^s) = (a*b) * 10^(2s); divide once by 10^s with Half-Even.
    // Work in the sign-magnitude domain to keep rounding symmetric.
    const bool negative = (value_ < 0) != (other.value_ < 0);

    UStorage lhs = value_ < 0
        ? static_cast<UStorage>(-value_)
        : static_cast<UStorage>(value_);
    UStorage rhs = other.value_ < 0
        ? static_cast<UStorage>(-other.value_)
        : static_cast<UStorage>(other.value_);

    if (lhs != 0 && rhs != 0) {
        // Overflow check on the raw product before scaling down.
        UStorage product = lhs * rhs;
        if (product / rhs != lhs) {
            return std::unexpected(DomainError::overflow("multiply"));
        }

        const UStorage scale = static_cast<UStorage>(kScaleFactor);
        UStorage quotient = product / scale;
        UStorage remainder = product % scale;

        // Half-Even rounding on the division by scale.
        const UStorage half = scale / 2;
        if (remainder > half || (remainder == half && (quotient & 1) != 0)) {
            ++quotient;
        }

        if (quotient > static_cast<UStorage>(kInt128Max)) {
            return std::unexpected(DomainError::overflow("multiply result out of range"));
        }

        Storage signed_result = static_cast<Storage>(quotient);
        return Decimal(negative ? -signed_result : signed_result);
    }

    return Decimal(0);
}

DomainResult<Decimal> Decimal::divide(const Decimal& other) const {
    if (other.value_ == 0) {
        return std::unexpected(DomainError::division_by_zero());
    }
    if (value_ == 0) {
        return Decimal(0);
    }

    // result = (a / b) * 10^s = (a * 10^s) / b, computed as
    // (value_ * 10^s) / other.value_ with Half-Even rounding.
    const bool negative = (value_ < 0) != (other.value_ < 0);

    UStorage num = value_ < 0
        ? static_cast<UStorage>(-value_)
        : static_cast<UStorage>(value_);
    UStorage den = other.value_ < 0
        ? static_cast<UStorage>(-other.value_)
        : static_cast<UStorage>(other.value_);

    const UStorage scale = static_cast<UStorage>(kScaleFactor);

    // Scale numerator by 10^s with overflow detection.
    UStorage scaled_num = num * scale;
    if (scaled_num / scale != num) {
        return std::unexpected(DomainError::overflow("divide"));
    }

    UStorage quotient = scaled_num / den;
    UStorage remainder = scaled_num % den;

    // Half-Even rounding: compare 2*remainder to den.
    UStorage twice_rem = remainder * 2;
    if (twice_rem > den || (twice_rem == den && (quotient & 1) != 0)) {
        ++quotient;
    }

    if (quotient > static_cast<UStorage>(kInt128Max)) {
        return std::unexpected(DomainError::overflow("divide result out of range"));
    }

    Storage signed_result = static_cast<Storage>(quotient);
    return Decimal(negative ? -signed_result : signed_result);
}

DomainResult<Decimal> Decimal::round_to_scale(
    std::int32_t fractional_digits) const {
    if (fractional_digits < 0 || fractional_digits > kScale) {
        return std::unexpected(DomainError::invalid_amount(
            "rounding scale must be between 0 and 10"));
    }
    if (fractional_digits == kScale || value_ == 0) {
        return *this;
    }

    Storage factor = 1;
    for (std::int32_t i = fractional_digits; i < kScale; ++i) {
        factor *= 10;
    }

    const bool negative = value_ < 0;
    const UStorage magnitude = negative
        ? static_cast<UStorage>(-(value_ + 1)) + 1
        : static_cast<UStorage>(value_);
    const UStorage unsigned_factor = static_cast<UStorage>(factor);
    UStorage quotient = magnitude / unsigned_factor;
    const UStorage remainder = magnitude % unsigned_factor;
    const UStorage twice_remainder = remainder * 2;
    if (twice_remainder > unsigned_factor ||
        (twice_remainder == unsigned_factor && (quotient & 1) != 0)) {
        ++quotient;
    }

    const UStorage rounded_magnitude = quotient * unsigned_factor;
    const UStorage signed_max =
        static_cast<UStorage>(std::numeric_limits<Storage>::max());
    if (rounded_magnitude > signed_max) {
        return std::unexpected(DomainError::overflow("round_to_scale"));
    }
    const Storage rounded = static_cast<Storage>(rounded_magnitude);
    return Decimal(negative ? -rounded : rounded);
}

namespace {

// Count trailing decimal zeros of a non-negative scaled magnitude, capped at
// kScale. Used to work out how many fractional digits the value actually uses.
[[nodiscard]] std::int32_t fractional_digits_used(UStorage scaled_magnitude) noexcept {
    if (scaled_magnitude == 0) {
        return 0;
    }
    std::int32_t trailing_zeros = 0;
    UStorage v = scaled_magnitude;
    while (trailing_zeros < Decimal::kScale && (v % 10) == 0) {
        v /= 10;
        ++trailing_zeros;
    }
    return Decimal::kScale - trailing_zeros;
}

// True if |value| fits NUMERIC(precision, scale): at most `scale` fractional
// digits and at most `precision - scale` integer digits.
[[nodiscard]] bool fits_numeric(Storage value, std::int32_t precision,
                                std::int32_t scale) noexcept {
    const UStorage magnitude = (value < 0)
        ? static_cast<UStorage>(-value)
        : static_cast<UStorage>(value);

    // Reject more fractional digits than the column allows (would round on write).
    if (fractional_digits_used(magnitude) > scale) {
        return false;
    }

    // Integer part = magnitude / 10^kScale must fit (precision - scale) digits.
    const UStorage scale_factor = static_cast<UStorage>(Decimal::kScaleFactor);
    UStorage integer_part = magnitude / scale_factor;
    const std::int32_t max_integer_digits = precision - scale;
    UStorage limit = 1;
    for (std::int32_t i = 0; i < max_integer_digits; ++i) {
        limit *= 10;
    }
    return integer_part < limit;
}

} // namespace

bool Decimal::fits_numeric_20_8() const noexcept {
    return fits_numeric(value_, 20, 8);
}

bool Decimal::fits_numeric_20_10() const noexcept {
    return fits_numeric(value_, 20, 10);
}

} // namespace pfh::domain
