// Personal Finance Hub - OpenSSL HS256 and Opaque Token Service

#include "pfh/infrastructure/security/openssl_token_service.h"

#ifdef PFH_HAS_POSTGRESQL

#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pfh::infrastructure {

namespace {

constexpr std::size_t kSha256Bytes = 32;

[[nodiscard]] application::Error invalid_token_error() {
    return application::Error(
        application::ErrorCode::InvalidToken,
        "Invalid or expired access token");
}

[[nodiscard]] application::Error crypto_error() {
    return application::Error::infrastructure_failure(
        "Token cryptography failed");
}

[[nodiscard]] std::string base64url_encode(
    const unsigned char* data,
    std::size_t size) {
    if (size == 0) {
        return {};
    }
    const auto encoded_size = 4 * ((size + 2) / 3);
    std::string encoded(encoded_size, '\0');
    const auto written = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(encoded.data()),
        data,
        static_cast<int>(size));
    if (written < 0) {
        return {};
    }
    encoded.resize(static_cast<std::size_t>(written));
    std::replace(encoded.begin(), encoded.end(), '+', '-');
    std::replace(encoded.begin(), encoded.end(), '/', '_');
    while (!encoded.empty() && encoded.back() == '=') {
        encoded.pop_back();
    }
    return encoded;
}

[[nodiscard]] std::string base64url_encode(std::string_view value) {
    return base64url_encode(
        reinterpret_cast<const unsigned char*>(value.data()), value.size());
}

[[nodiscard]] application::Result<std::string> base64url_decode(
    std::string_view value) {
    if (value.empty() || value.size() > 16'384) {
        return application::err(invalid_token_error());
    }
    std::string padded(value);
    for (char& c : padded) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9'))) {
            return application::err(invalid_token_error());
        }
    }
    const auto padding = (4 - (padded.size() % 4)) % 4;
    padded.append(padding, '=');
    std::string decoded((padded.size() / 4) * 3, '\0');
    const auto written = EVP_DecodeBlock(
        reinterpret_cast<unsigned char*>(decoded.data()),
        reinterpret_cast<const unsigned char*>(padded.data()),
        static_cast<int>(padded.size()));
    if (written < 0 || static_cast<std::size_t>(written) < padding) {
        return application::err(invalid_token_error());
    }
    decoded.resize(static_cast<std::size_t>(written) - padding);
    return decoded;
}

[[nodiscard]] application::Result<std::array<unsigned char, kSha256Bytes>>
hmac_sha256(std::string_view secret, std::string_view value) {
    if (secret.empty()) {
        return application::err(crypto_error());
    }
    using PKeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using MdContextPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    PKeyPtr key(
        EVP_PKEY_new_raw_private_key(
            EVP_PKEY_HMAC,
            nullptr,
            reinterpret_cast<const unsigned char*>(secret.data()),
            secret.size()),
        EVP_PKEY_free);
    MdContextPtr context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!key || !context ||
        EVP_DigestSignInit(
            context.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1 ||
        EVP_DigestSignUpdate(context.get(), value.data(), value.size()) != 1) {
        return application::err(crypto_error());
    }
    std::array<unsigned char, kSha256Bytes> digest{};
    std::size_t length = digest.size();
    if (EVP_DigestSignFinal(context.get(), digest.data(), &length) != 1 ||
        length != digest.size()) {
        return application::err(crypto_error());
    }
    return digest;
}

[[nodiscard]] std::int64_t epoch_seconds(application::AuthTimePoint value) {
    return std::chrono::duration_cast<std::chrono::seconds>(
        value.time_since_epoch()).count();
}

[[nodiscard]] application::AuthTimePoint from_epoch(std::int64_t value) {
    return application::AuthTimePoint(std::chrono::seconds(value));
}

[[nodiscard]] bool valid_identifier(std::string_view value, std::size_t max) {
    return !value.empty() && value.size() <= max &&
           value.find_first_of(" \t\r\n") == std::string_view::npos;
}

} // namespace

OpenSslTokenService::~OpenSslTokenService() {
    if (!secret_.empty()) {
        OPENSSL_cleanse(secret_.data(), secret_.size());
    }
}

application::Result<std::string> OpenSslTokenService::generate_opaque_token(
    std::size_t byte_count) const {
    if (byte_count < 16 || byte_count > 1024 ||
        byte_count > static_cast<std::size_t>(INT_MAX)) {
        return application::err(application::Error::validation(
            "Invalid secure token length"));
    }
    std::vector<unsigned char> bytes(byte_count);
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        return application::err(crypto_error());
    }
    const auto token = base64url_encode(bytes.data(), bytes.size());
    OPENSSL_cleanse(bytes.data(), bytes.size());
    if (token.empty()) {
        return application::err(crypto_error());
    }
    return token;
}

application::Result<std::string> OpenSslTokenService::hash_opaque_token(
    std::string_view token) const {
    if (token.empty() || token.size() > 4096) {
        return application::err(application::Error(
            application::ErrorCode::InvalidToken,
            "Invalid refresh token"));
    }
    std::array<unsigned char, kSha256Bytes> digest{};
    unsigned int digest_size = 0;
    if (EVP_Digest(
            reinterpret_cast<const unsigned char*>(token.data()),
            token.size(), digest.data(), &digest_size, EVP_sha256(), nullptr) != 1 ||
        digest_size != digest.size()) {
        return application::err(crypto_error());
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(kHex[(byte >> 4) & 0x0f]);
        result.push_back(kHex[byte & 0x0f]);
    }
    OPENSSL_cleanse(digest.data(), digest.size());
    return result;
}

application::Result<application::IssuedAccessToken>
OpenSslTokenService::issue_access_token(
    domain::UserId user_id,
    std::string_view session_id,
    application::AuthTimePoint issued_at) const {
    if (secret_.size() < 32 || issuer_.empty() || audience_.empty() ||
        !user_id.is_valid() || !valid_identifier(session_id, 64) ||
        access_lifetime_ <= std::chrono::seconds::zero()) {
        return application::err(crypto_error());
    }
    auto token_id = generate_opaque_token(16);
    if (!token_id) {
        return application::err(token_id.error());
    }

    application::AccessTokenClaims claims;
    claims.issuer = issuer_;
    claims.audience = audience_;
    claims.user_id = user_id;
    claims.session_id = std::string(session_id);
    claims.token_id = *token_id;
    claims.issued_at = issued_at;
    claims.not_before = issued_at;
    claims.expires_at = issued_at + access_lifetime_;

    const nlohmann::json header{{"alg", "HS256"}, {"typ", "JWT"}};
    const nlohmann::json payload{
        {"iss", claims.issuer},
        {"aud", claims.audience},
        {"sub", claims.user_id.to_string()},
        {"sid", claims.session_id},
        {"jti", claims.token_id},
        {"roles", nlohmann::json::array({"USER"})},
        {"iat", epoch_seconds(claims.issued_at)},
        {"nbf", epoch_seconds(claims.not_before)},
        {"exp", epoch_seconds(claims.expires_at)}};
    const auto signing_input =
        base64url_encode(header.dump()) + "." + base64url_encode(payload.dump());
    auto signature = hmac_sha256(secret_, signing_input);
    if (!signature) {
        return application::err(signature.error());
    }
    const auto token = signing_input + "." +
                       base64url_encode(signature->data(), signature->size());
    OPENSSL_cleanse(signature->data(), signature->size());
    return application::IssuedAccessToken{token, claims};
}

application::Result<application::AccessTokenClaims>
OpenSslTokenService::validate_access_token(
    std::string_view token,
    application::AuthTimePoint now) const {
    if (secret_.size() < 32 || token.empty() || token.size() > 8192) {
        return application::err(invalid_token_error());
    }
    const auto first_dot = token.find('.');
    const auto second_dot = first_dot == std::string_view::npos
        ? std::string_view::npos
        : token.find('.', first_dot + 1);
    if (first_dot == std::string_view::npos ||
        second_dot == std::string_view::npos ||
        token.find('.', second_dot + 1) != std::string_view::npos ||
        first_dot == 0 || second_dot == first_dot + 1 ||
        second_dot + 1 >= token.size()) {
        return application::err(invalid_token_error());
    }

    const auto header_segment = token.substr(0, first_dot);
    const auto payload_segment = token.substr(first_dot + 1, second_dot - first_dot - 1);
    const auto signature_segment = token.substr(second_dot + 1);
    auto header_text = base64url_decode(header_segment);
    auto signature = base64url_decode(signature_segment);
    if (!header_text || !signature) {
        return application::err(invalid_token_error());
    }

    try {
        const auto header = nlohmann::json::parse(*header_text);
        if (!header.is_object() || header.value("alg", "") != "HS256" ||
            header.value("typ", "") != "JWT" || header.contains("crit")) {
            return application::err(invalid_token_error());
        }
    } catch (const nlohmann::json::exception&) {
        return application::err(invalid_token_error());
    }

    const auto signing_input = token.substr(0, second_dot);
    auto expected_signature = hmac_sha256(secret_, signing_input);
    if (!expected_signature || signature->size() != expected_signature->size() ||
        CRYPTO_memcmp(
            signature->data(),
            expected_signature->data(),
            expected_signature->size()) != 0) {
        if (expected_signature) {
            OPENSSL_cleanse(expected_signature->data(), expected_signature->size());
        }
        return application::err(invalid_token_error());
    }
    OPENSSL_cleanse(expected_signature->data(), expected_signature->size());

    auto payload_text = base64url_decode(payload_segment);
    if (!payload_text) {
        return application::err(invalid_token_error());
    }
    try {
        const auto payload = nlohmann::json::parse(*payload_text);
        const std::array required{
            "iss", "aud", "sub", "sid", "jti", "roles", "iat", "nbf", "exp"};
        if (!payload.is_object() ||
            std::any_of(required.begin(), required.end(), [&](const char* field) {
                return !payload.contains(field);
            }) ||
            !payload["iss"].is_string() || !payload["aud"].is_string() ||
            !payload["sub"].is_string() || !payload["sid"].is_string() ||
            !payload["jti"].is_string() || !payload["roles"].is_array() ||
            !payload["iat"].is_number_integer() ||
            !payload["nbf"].is_number_integer() || !payload["exp"].is_number_integer()) {
            return application::err(invalid_token_error());
        }
        const auto has_user_role = std::any_of(
            payload["roles"].begin(), payload["roles"].end(), [](const auto& role) {
                return role.is_string() && role.template get<std::string>() == "USER";
            });
        if (!has_user_role) {
            return application::err(invalid_token_error());
        }
        if (payload["iss"].get<std::string>() != issuer_ ||
            payload["aud"].get<std::string>() != audience_) {
            return application::err(invalid_token_error());
        }
        const auto subject = payload["sub"].get<std::string>();
        std::int64_t user_value = 0;
        const auto parsed = std::from_chars(
            subject.data(), subject.data() + subject.size(), user_value);
        const domain::UserId user_id(user_value);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != subject.data() + subject.size() ||
            !user_id.is_valid() || subject != user_id.to_string()) {
            return application::err(invalid_token_error());
        }

        application::AccessTokenClaims claims;
        claims.issuer = issuer_;
        claims.audience = audience_;
        claims.user_id = user_id;
        claims.session_id = payload["sid"].get<std::string>();
        claims.token_id = payload["jti"].get<std::string>();
        claims.issued_at = from_epoch(payload["iat"].get<std::int64_t>());
        claims.not_before = from_epoch(payload["nbf"].get<std::int64_t>());
        claims.expires_at = from_epoch(payload["exp"].get<std::int64_t>());
        if (!valid_identifier(claims.session_id, 64) ||
            !valid_identifier(claims.token_id, 128) ||
            claims.expires_at <= claims.issued_at ||
            claims.not_before < claims.issued_at ||
            claims.not_before >= claims.expires_at ||
            claims.expires_at <= now ||
            claims.not_before > now + clock_skew_ ||
            claims.issued_at > now + clock_skew_) {
            return application::err(invalid_token_error());
        }
        return claims;
    } catch (const nlohmann::json::exception&) {
        return application::err(invalid_token_error());
    }
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
