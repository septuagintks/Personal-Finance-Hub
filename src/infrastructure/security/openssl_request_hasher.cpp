// Personal Finance Hub - OpenSSL Request Fingerprint Hasher

#include "pfh/infrastructure/security/openssl_request_hasher.h"

#ifdef PFH_HAS_POSTGRESQL

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <array>
#include <string>

namespace pfh::infrastructure {

application::Result<std::string> OpenSslRequestHasher::sha256(
    std::string_view value) const {
    std::array<unsigned char, 32> digest{};
    unsigned int digest_size = 0;
    if (EVP_Digest(
            reinterpret_cast<const unsigned char*>(value.data()),
            value.size(),
            digest.data(),
            &digest_size,
            EVP_sha256(),
            nullptr) != 1 ||
        digest_size != digest.size()) {
        return application::err(application::Error::infrastructure_failure(
            "Request fingerprint generation failed"));
    }

    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(kHex[(byte >> 4U) & 0x0fU]);
        result.push_back(kHex[byte & 0x0fU]);
    }
    OPENSSL_cleanse(digest.data(), digest.size());
    return result;
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
