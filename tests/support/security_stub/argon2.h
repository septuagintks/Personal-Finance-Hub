// Compile-only Argon2 API subset.
#pragma once

#include <cstddef>
#include <cstdint>

enum argon2_type { Argon2_d, Argon2_i, Argon2_id };

inline constexpr int ARGON2_OK = 0;
inline constexpr int ARGON2_VERIFY_MISMATCH = -35;

inline std::size_t argon2_encodedlen(
    std::uint32_t, std::uint32_t, std::uint32_t,
    std::uint32_t, std::uint32_t, argon2_type) {
    return 128;
}

inline int argon2id_hash_encoded(
    std::uint32_t, std::uint32_t, std::uint32_t,
    const void*, std::size_t, const void*, std::size_t,
    std::size_t, char*, std::size_t) {
    return ARGON2_OK;
}

inline int argon2id_verify(const char*, const void*, std::size_t) {
    return ARGON2_OK;
}
