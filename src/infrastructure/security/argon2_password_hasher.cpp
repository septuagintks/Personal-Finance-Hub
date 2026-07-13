// Personal Finance Hub - Argon2id Password Hasher

#include "pfh/infrastructure/security/argon2_password_hasher.h"

#ifdef PFH_HAS_POSTGRESQL

#include <argon2.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pfh::infrastructure {

namespace {

constexpr std::uint32_t kIterations = 2;
constexpr std::uint32_t kMemoryKiB = 19'456;
constexpr std::uint32_t kParallelism = 1;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

void cleanse(std::string& value) noexcept {
    if (!value.empty()) {
        OPENSSL_cleanse(value.data(), value.size());
    }
}

[[nodiscard]] application::Error hashing_failure() {
    return application::Error::infrastructure_failure(
        "Password hashing failed");
}

} // namespace

Argon2PasswordHasher::~Argon2PasswordHasher() {
    cleanse(pepper_);
}

application::Result<std::string> Argon2PasswordHasher::hash(
    std::string_view plaintext_password) const {
    if (plaintext_password.empty()) {
        return application::err(application::Error::validation(
            "Password must not be empty"));
    }
    std::array<unsigned char, kSaltBytes> salt{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        return application::err(hashing_failure());
    }

    std::string secret(plaintext_password);
    secret += pepper_;
    const auto encoded_length = argon2_encodedlen(
        kIterations,
        kMemoryKiB,
        kParallelism,
        static_cast<std::uint32_t>(salt.size()),
        static_cast<std::uint32_t>(kHashBytes),
        Argon2_id);
    std::vector<char> encoded(encoded_length, '\0');
    const auto status = argon2id_hash_encoded(
        kIterations,
        kMemoryKiB,
        kParallelism,
        secret.data(),
        secret.size(),
        salt.data(),
        salt.size(),
        kHashBytes,
        encoded.data(),
        encoded.size());
    cleanse(secret);
    OPENSSL_cleanse(salt.data(), salt.size());
    if (status != ARGON2_OK) {
        return application::err(hashing_failure());
    }
    return std::string(encoded.data());
}

application::Result<bool> Argon2PasswordHasher::verify(
    std::string_view plaintext_password,
    std::string_view encoded_hash) const {
    if (plaintext_password.empty() || encoded_hash.empty()) {
        return false;
    }
    if (!encoded_hash.starts_with("$argon2id$")) {
        return application::err(application::Error::infrastructure_failure(
            "Stored password hash uses an unsupported format"));
    }
    std::string secret(plaintext_password);
    secret += pepper_;
    const std::string encoded(encoded_hash);
    const auto status = argon2id_verify(
        encoded.c_str(), secret.data(), secret.size());
    cleanse(secret);
    if (status == ARGON2_OK) {
        return true;
    }
    if (status == ARGON2_VERIFY_MISMATCH) {
        return false;
    }
    return application::err(hashing_failure());
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
