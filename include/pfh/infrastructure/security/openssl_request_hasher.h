// Personal Finance Hub - OpenSSL Request Fingerprint Hasher

#pragma once

#include "pfh/application/ports/i_request_hasher.h"

#ifdef PFH_HAS_POSTGRESQL

namespace pfh::infrastructure {

class OpenSslRequestHasher final : public application::IRequestHasher {
public:
    [[nodiscard]] application::Result<std::string> sha256(
        std::string_view value) const override;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
