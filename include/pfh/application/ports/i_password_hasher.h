// Personal Finance Hub - Password Hashing Port

#pragma once

#include "pfh/application/error.h"

#include <string>
#include <string_view>

namespace pfh::application {

class IPasswordHasher {
public:
    virtual ~IPasswordHasher() = default;

    [[nodiscard]] virtual Result<std::string> hash(
        std::string_view plaintext_password) const = 0;
    [[nodiscard]] virtual Result<bool> verify(
        std::string_view plaintext_password,
        std::string_view encoded_hash) const = 0;
};

} // namespace pfh::application
