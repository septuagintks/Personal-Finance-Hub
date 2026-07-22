// Personal Finance Hub - Application Input Constraints

#pragma once

#include <cstddef>
#include <string_view>

namespace pfh::application {

inline constexpr std::size_t kMaxDecimalInputLength = 128;
inline constexpr std::size_t kMaxDescriptionLength = 4096;
inline constexpr std::size_t kMaxLocaleTagLength = 16;
inline constexpr std::size_t kReportPageSize = 200;
inline constexpr std::size_t kMaximumAggregateReportRows = 100'000;
inline constexpr std::size_t kMaximumDetailedReportRows = 10'000;
inline constexpr std::size_t kMaximumReportInputBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kMaximumCsvOutputBytes = 32U * 1024U * 1024U;
inline constexpr std::size_t kMaximumBreakdownBuckets = 10'000;
inline constexpr std::size_t kMaximumBreakdownExpansions = 100'000;
inline constexpr std::size_t kMaximumReportMetadataItems = 10'000;
inline constexpr std::size_t kMaximumReportMonths = 120;
inline constexpr std::size_t kMaximumCsvRangeDays = 366;

/// Locale tags use a deliberately small ASCII subset at the public boundary:
/// non-empty alphanumeric segments separated by single hyphens. This accepts
/// the Phase 1 values (for example, zh-CN and en-US) without admitting values
/// that cannot be represented by the OpenAPI contract.
[[nodiscard]] inline bool is_locale_tag(std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaxLocaleTagLength) {
        return false;
    }

    bool segment_has_character = false;
    for (const char c : value) {
        const bool is_ascii_alphanumeric =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9');
        if (is_ascii_alphanumeric) {
            segment_has_character = true;
            continue;
        }
        if (c != '-' || !segment_has_character) {
            return false;
        }
        segment_has_character = false;
    }
    return segment_has_character;
}

/// Public command amounts use the same plain-decimal grammar as OpenAPI.
/// Domain Decimal remains intentionally more permissive for internal parsing.
[[nodiscard]] inline bool is_plain_decimal_string(
    std::string_view value,
    bool allow_negative) noexcept {
    if (value.empty() || value.size() > kMaxDecimalInputLength) {
        return false;
    }

    std::size_t index = 0;
    if (allow_negative && value.front() == '-') {
        index = 1;
    }
    if (index == value.size()) {
        return false;
    }

    bool saw_decimal_point = false;
    std::size_t integer_digits = 0;
    std::size_t fractional_digits = 0;
    for (; index < value.size(); ++index) {
        const char c = value[index];
        if (c == '.' && !saw_decimal_point) {
            saw_decimal_point = true;
            continue;
        }
        if (c < '0' || c > '9') {
            return false;
        }
        if (saw_decimal_point) {
            ++fractional_digits;
        } else {
            ++integer_digits;
        }
    }
    return integer_digits > 0 &&
           (!saw_decimal_point || fractional_digits > 0);
}

} // namespace pfh::application
