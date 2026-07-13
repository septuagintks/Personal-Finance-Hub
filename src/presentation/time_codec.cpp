// Personal Finance Hub - RFC 3339 Time Codec

#include "pfh/presentation/http/time_codec.h"

#include <charconv>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

namespace pfh::presentation {

namespace {

[[nodiscard]] application::Error invalid_time() {
    return application::Error(
        application::ErrorCode::InvalidFormat,
        "Timestamp must use RFC 3339 format");
}

[[nodiscard]] bool parse_fixed_int(
    std::string_view value,
    std::size_t offset,
    std::size_t length,
    int& result) {
    if (offset + length > value.size()) {
        return false;
    }
    const auto* first = value.data() + offset;
    const auto* last = first + length;
    const auto parsed = std::from_chars(first, last, result);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

} // namespace

application::Result<std::chrono::system_clock::time_point>
TimeCodec::parse_rfc3339(std::string_view value) {
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        (value[10] != 'T' && value[10] != 't') || value[13] != ':' ||
        value[16] != ':') {
        return application::err(invalid_time());
    }

    int year_value = 0;
    int month_value = 0;
    int day_value = 0;
    int hour_value = 0;
    int minute_value = 0;
    int second_value = 0;
    if (!parse_fixed_int(value, 0, 4, year_value) ||
        !parse_fixed_int(value, 5, 2, month_value) ||
        !parse_fixed_int(value, 8, 2, day_value) ||
        !parse_fixed_int(value, 11, 2, hour_value) ||
        !parse_fixed_int(value, 14, 2, minute_value) ||
        !parse_fixed_int(value, 17, 2, second_value)) {
        return application::err(invalid_time());
    }
    if (hour_value > 23 || minute_value > 59 || second_value > 59) {
        return application::err(invalid_time());
    }

    const std::chrono::year_month_day date{
        std::chrono::year(year_value),
        std::chrono::month(static_cast<unsigned>(month_value)),
        std::chrono::day(static_cast<unsigned>(day_value))};
    if (!date.ok()) {
        return application::err(invalid_time());
    }

    std::size_t position = 19;
    std::chrono::nanoseconds fraction{0};
    if (position < value.size() && value[position] == '.') {
        ++position;
        const auto fraction_start = position;
        while (position < value.size() && value[position] >= '0' &&
               value[position] <= '9') {
            ++position;
        }
        const auto digits = position - fraction_start;
        if (digits == 0 || digits > 9) {
            return application::err(invalid_time());
        }
        std::int64_t fraction_value = 0;
        const auto parsed = std::from_chars(
            value.data() + fraction_start,
            value.data() + position,
            fraction_value);
        if (parsed.ec != std::errc{}) {
            return application::err(invalid_time());
        }
        for (std::size_t i = digits; i < 9; ++i) {
            fraction_value *= 10;
        }
        fraction = std::chrono::nanoseconds(fraction_value);
    }

    std::chrono::minutes offset{0};
    if (position >= value.size()) {
        return application::err(invalid_time());
    }
    if (value[position] == 'Z' || value[position] == 'z') {
        ++position;
    } else if (value[position] == '+' || value[position] == '-') {
        const bool negative = value[position] == '-';
        if (position + 6 != value.size() || value[position + 3] != ':') {
            return application::err(invalid_time());
        }
        int offset_hours = 0;
        int offset_minutes = 0;
        if (!parse_fixed_int(value, position + 1, 2, offset_hours) ||
            !parse_fixed_int(value, position + 4, 2, offset_minutes) ||
            offset_hours > 23 || offset_minutes > 59) {
            return application::err(invalid_time());
        }
        offset = std::chrono::hours(offset_hours) +
                 std::chrono::minutes(offset_minutes);
        if (negative) {
            offset = -offset;
        }
        position += 6;
    } else {
        return application::err(invalid_time());
    }
    if (position != value.size()) {
        return application::err(invalid_time());
    }

    const auto local = std::chrono::sys_days(date) +
                       std::chrono::hours(hour_value) +
                       std::chrono::minutes(minute_value) +
                       std::chrono::seconds(second_value) + fraction;
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        local - offset);
}

std::string TimeCodec::format_rfc3339(
    std::chrono::system_clock::time_point value) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(value);
    const auto days = std::chrono::floor<std::chrono::days>(seconds);
    const std::chrono::year_month_day date(days);
    const std::chrono::hh_mm_ss time(seconds - days);

    std::ostringstream output;
    output << std::setfill('0')
           << std::setw(4) << static_cast<int>(date.year()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.month()) << '-'
           << std::setw(2) << static_cast<unsigned>(date.day()) << 'T'
           << std::setw(2) << time.hours().count() << ':'
           << std::setw(2) << time.minutes().count() << ':'
           << std::setw(2) << time.seconds().count() << 'Z';
    return output.str();
}

} // namespace pfh::presentation
