// Personal Finance Hub - Request Fingerprint Port

#pragma once

#include "pfh/application/error.h"

#include <string>
#include <string_view>

namespace pfh::application {

class IRequestHasher {
public:
    virtual ~IRequestHasher() = default;
    [[nodiscard]] virtual Result<std::string> sha256(
        std::string_view value) const = 0;
};

} // namespace pfh::application
