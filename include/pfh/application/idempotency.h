// Personal Finance Hub - Idempotency Contract Helpers

#pragma once

#include "pfh/application/error.h"

#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace pfh::application {

inline constexpr std::size_t kMaxIdempotencyKeyLength = 128;
inline constexpr std::chrono::hours kIdempotencyLifetime{24};

struct IdempotencyRequest {
    std::string key;
    std::string request_fingerprint;
    std::chrono::system_clock::time_point created_at{};
};

using IdempotencyValues = std::map<std::string, std::string>;

[[nodiscard]] inline VoidResult validate_idempotency_input(
    std::string_view key,
    std::string_view fingerprint) {
    if (key.empty() || key.size() > kMaxIdempotencyKeyLength) {
        return err(Error::validation(
            "Idempotency-Key must contain 1 to 128 characters"));
    }
    for (const char raw : key) {
        const auto value = static_cast<unsigned char>(raw);
        if (value < 0x21U || value > 0x7eU) {
            return err(Error::validation(
                "Idempotency-Key must contain visible ASCII characters"));
        }
    }
    if (fingerprint.size() != 64U) {
        return err(Error::validation("Request fingerprint is invalid"));
    }
    for (const char raw : fingerprint) {
        const auto value = static_cast<unsigned char>(raw);
        if (std::isxdigit(value) == 0 || std::isupper(value) != 0) {
            return err(Error::validation("Request fingerprint is invalid"));
        }
    }
    return ok();
}

inline void append_canonical_field(std::string& output, std::string_view value) {
    output += std::to_string(value.size());
    output.push_back(':');
    output.append(value);
}

[[nodiscard]] inline std::optional<std::string_view> idempotency_value(
    const IdempotencyValues& values,
    std::string_view name) {
    const auto found = values.find(std::string(name));
    if (found == values.end()) {
        return std::nullopt;
    }
    return found->second;
}

[[nodiscard]] inline std::optional<std::int64_t> parse_idempotency_integer(
    std::string_view value) {
    std::int64_t parsed = 0;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] inline std::string encode_idempotency_time(
    std::chrono::system_clock::time_point value) {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
        value.time_since_epoch()).count());
}

[[nodiscard]] inline std::optional<std::chrono::system_clock::time_point>
decode_idempotency_time(std::string_view value) {
    const auto micros = parse_idempotency_integer(value);
    if (!micros.has_value()) {
        return std::nullopt;
    }
    return std::chrono::system_clock::time_point(
        std::chrono::microseconds(*micros));
}

} // namespace pfh::application
