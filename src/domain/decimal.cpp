// Personal Finance Hub - Decimal Fixed-Point Type Implementation
// Version: 1.0
// C++23

#include "pfh/domain/decimal.h"
#include <array>
#include <charconv>
#include <cstdlib>

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

constexpr UStorage kSafeMagnitude =
    static_cast<UStorage>(Decimal::kMaxRawValue);

/// @brief Return |value| without ever negating a signed integer.
[[nodiscard]] constexpr UStorage unsigned_magnitude(Storage value) noexcept {
    const UStorage bits = static_cast<UStorage>(value);
    return value < 0 ? UStorage{0} - bits : bits;
}

[[nodiscard]] constexpr bool is_valid_raw(Storage value) noexcept {
    return value >= Decimal::kMinRawValue &&
           value <= Decimal::kMaxRawValue;
}

[[nodiscard]] bool storage_from_magnitude(UStorage magnitude, bool negative,
                                          Storage& out) noexcept {
    if (magnitude > kSafeMagnitude) {
        return false;
    }
    const Storage signed_magnitude = static_cast<Storage>(magnitude);
    out = negative ? -signed_magnitude : signed_magnitude;
    return true;
}

/// @brief Multiply two 128-bit values, detecting overflow.
///
/// Signed overflow is undefined behaviour, so multiplication is performed in
/// the unsigned magnitude domain and bounded before it happens.
[[nodiscard]] bool checked_mul(Storage a, Storage b, Storage& out) noexcept {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    const bool negative = (a < 0) != (b < 0);
    const UStorage ua = unsigned_magnitude(a);
    const UStorage ub = unsigned_magnitude(b);
    if (ua > kSafeMagnitude / ub) {
        return false;
    }
    return storage_from_magnitude(ua * ub, negative, out);
}

/// @brief Add two valid raw values while preserving the safe-range invariant.
[[nodiscard]] bool checked_add(Storage a, Storage b, Storage& out) noexcept {
    if ((b > 0 && a > Decimal::kMaxRawValue - b) ||
        (b < 0 && a < Decimal::kMinRawValue - b)) {
        return false;
    }
    out = a + b;
    return true;
}

[[nodiscard]] bool checked_uadd_bounded(UStorage a, UStorage b,
                                        UStorage bound,
                                        UStorage& out) noexcept {
    if (a > bound || b > bound - a) {
        return false;
    }
    out = a + b;
    return true;
}

[[nodiscard]] bool checked_umul_bounded(UStorage a, UStorage b,
                                        UStorage bound,
                                        UStorage& out) noexcept {
    if (a != 0 && b > bound / a) {
        return false;
    }
    out = a * b;
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

DomainResult<Decimal> Decimal::from_scaled(StorageType scaled) {
    if (!is_valid_raw(scaled)) {
        return std::unexpected(DomainError::overflow(
            "raw value outside Decimal safe range"));
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

    // Build magnitude = int_part * 10^kScale + frac_part in the unsigned
    // domain. Reject as soon as the public Decimal range would be exceeded.
    UStorage magnitude = 0;
    auto push_digit = [&](char digit) -> bool {
        const UStorage value = static_cast<UStorage>(digit - '0');
        if (magnitude > (kSafeMagnitude - value) / 10) {
            return false;
        }
        magnitude = magnitude * 10 + value;
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
        if (magnitude == kSafeMagnitude) {
            return std::unexpected(DomainError::overflow("rounding carry"));
        }
        ++magnitude;
    }

    Storage scaled = 0;
    if (!storage_from_magnitude(magnitude, negative, scaled)) {
        return std::unexpected(DomainError::overflow("value out of range"));
    }
    return Decimal(scaled);
}

std::string Decimal::to_string() const {
    if (value_ == 0) {
        return "0";
    }

    const bool negative = value_ < 0;
    const UStorage magnitude = unsigned_magnitude(value_);

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
    Storage result = 0;
    if (!checked_add(value_, -other.value_, result)) {
        return std::unexpected(DomainError::overflow("subtract"));
    }
    return Decimal(result);
}

DomainResult<Decimal> Decimal::multiply(const Decimal& other) const {
    // Split each magnitude around the scale before multiplying. Every term is
    // non-negative, so a valid final result can be assembled without requiring
    // a temporary 256-bit product.
    const bool negative = (value_ < 0) != (other.value_ < 0);
    const UStorage lhs = unsigned_magnitude(value_);
    const UStorage rhs = unsigned_magnitude(other.value_);
    if (lhs == 0 || rhs == 0) {
        return Decimal(0);
    }

    const UStorage scale = static_cast<UStorage>(kScaleFactor);
    const UStorage lhs_whole = lhs / scale;
    const UStorage lhs_fraction = lhs % scale;
    const UStorage rhs_whole = rhs / scale;
    const UStorage rhs_fraction = rhs % scale;

    UStorage result = 0;
    UStorage term = 0;
    UStorage whole_product = 0;
    if (!checked_umul_bounded(lhs_whole, rhs_whole,
                              kSafeMagnitude / scale, whole_product)) {
        return std::unexpected(DomainError::overflow("multiply"));
    }
    result = whole_product * scale;

    auto add_product = [&](UStorage a, UStorage b) -> bool {
        if (!checked_umul_bounded(a, b, kSafeMagnitude - result, term)) {
            return false;
        }
        return checked_uadd_bounded(result, term, kSafeMagnitude, result);
    };
    if (!add_product(lhs_whole, rhs_fraction) ||
        !add_product(rhs_whole, lhs_fraction)) {
        return std::unexpected(DomainError::overflow("multiply"));
    }

    const UStorage fractional_product = lhs_fraction * rhs_fraction;
    const UStorage fractional_quotient = fractional_product / scale;
    const UStorage remainder = fractional_product % scale;
    if (!checked_uadd_bounded(result, fractional_quotient,
                              kSafeMagnitude, result)) {
        return std::unexpected(DomainError::overflow("multiply"));
    }

    const UStorage half = scale / 2;
    if (remainder > half || (remainder == half && (result & 1) != 0)) {
        if (result == kSafeMagnitude) {
            return std::unexpected(DomainError::overflow("multiply rounding"));
        }
        ++result;
    }

    Storage signed_result = 0;
    if (!storage_from_magnitude(result, negative, signed_result)) {
        return std::unexpected(DomainError::overflow("multiply"));
    }
    return Decimal(signed_result);
}

DomainResult<Decimal> Decimal::divide(const Decimal& other) const {
    if (other.value_ == 0) {
        return std::unexpected(DomainError::division_by_zero());
    }
    if (value_ == 0) {
        return Decimal(0);
    }

    // Produce the ten fixed-point digits with long division. This avoids the
    // potentially overflowing temporary num * 10^kScale.
    const bool negative = (value_ < 0) != (other.value_ < 0);
    const UStorage numerator = unsigned_magnitude(value_);
    const UStorage denominator = unsigned_magnitude(other.value_);
    UStorage quotient = numerator / denominator;
    UStorage remainder = numerator % denominator;

    for (std::int32_t i = 0; i < kScale; ++i) {
        unsigned digit = 0;
        UStorage next_remainder = 0;
        for (unsigned multiple = 0; multiple < 10; ++multiple) {
            if (next_remainder >= denominator - remainder) {
                next_remainder -= denominator - remainder;
                ++digit;
            } else {
                next_remainder += remainder;
            }
        }
        if (quotient >
            (kSafeMagnitude - static_cast<UStorage>(digit)) / 10) {
            return std::unexpected(DomainError::overflow("divide"));
        }
        quotient = quotient * 10 + static_cast<UStorage>(digit);
        remainder = next_remainder;
    }

    const UStorage twice_remainder = remainder * 2;
    if (twice_remainder > denominator ||
        (twice_remainder == denominator && (quotient & 1) != 0)) {
        if (quotient == kSafeMagnitude) {
            return std::unexpected(DomainError::overflow("divide rounding"));
        }
        ++quotient;
    }

    Storage signed_result = 0;
    if (!storage_from_magnitude(quotient, negative, signed_result)) {
        return std::unexpected(DomainError::overflow("divide"));
    }
    return Decimal(signed_result);
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
    const UStorage magnitude = unsigned_magnitude(value_);
    const UStorage unsigned_factor = static_cast<UStorage>(factor);
    UStorage quotient = magnitude / unsigned_factor;
    const UStorage remainder = magnitude % unsigned_factor;
    const UStorage twice_remainder = remainder * 2;
    if (twice_remainder > unsigned_factor ||
        (twice_remainder == unsigned_factor && (quotient & 1) != 0)) {
        ++quotient;
    }

    UStorage rounded_magnitude = 0;
    if (!checked_umul_bounded(quotient, unsigned_factor,
                              kSafeMagnitude, rounded_magnitude)) {
        return std::unexpected(DomainError::overflow("round_to_scale"));
    }
    Storage rounded = 0;
    if (!storage_from_magnitude(rounded_magnitude, negative, rounded)) {
        return std::unexpected(DomainError::overflow("round_to_scale"));
    }
    return Decimal(rounded);
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
    const UStorage magnitude = unsigned_magnitude(value);

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
