// Personal Finance Hub - Argon2id Password Hasher

#pragma once

#include "pfh/application/ports/i_password_hasher.h"

#ifdef PFH_HAS_POSTGRESQL

#include <string>
#include <utility>

namespace pfh::infrastructure {

class Argon2PasswordHasher final : public application::IPasswordHasher {
public:
    explicit Argon2PasswordHasher(std::string pepper = {})
        : pepper_(std::move(pepper)) {}
    ~Argon2PasswordHasher() override;

    [[nodiscard]] application::Result<std::string> hash(
        std::string_view plaintext_password) const override;

    [[nodiscard]] application::Result<bool> verify(
        std::string_view plaintext_password,
        std::string_view encoded_hash) const override;

private:
    std::string pepper_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
