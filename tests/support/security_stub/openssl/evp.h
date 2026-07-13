// Compile-only OpenSSL EVP API subset.
#pragma once

#include <cstddef>

struct evp_md_st {};
using EVP_MD = evp_md_st;
struct evp_pkey_st {};
using EVP_PKEY = evp_pkey_st;
struct evp_md_ctx_st {};
using EVP_MD_CTX = evp_md_ctx_st;

inline constexpr int EVP_PKEY_HMAC = 855;

inline const EVP_MD* EVP_sha256() {
    static EVP_MD md;
    return &md;
}

inline int EVP_EncodeBlock(unsigned char*, const unsigned char*, int length) {
    return ((length + 2) / 3) * 4;
}

inline int EVP_DecodeBlock(unsigned char*, const unsigned char*, int length) {
    return (length / 4) * 3;
}

inline EVP_PKEY* EVP_PKEY_new_raw_private_key(
    int, void*, const unsigned char*, std::size_t) {
    return new EVP_PKEY;
}
inline void EVP_PKEY_free(EVP_PKEY* value) { delete value; }
inline EVP_MD_CTX* EVP_MD_CTX_new() { return new EVP_MD_CTX; }
inline void EVP_MD_CTX_free(EVP_MD_CTX* value) { delete value; }
inline int EVP_DigestSignInit(
    EVP_MD_CTX*, void*, const EVP_MD*, void*, EVP_PKEY*) { return 1; }
inline int EVP_DigestSignUpdate(EVP_MD_CTX*, const void*, std::size_t) { return 1; }
inline int EVP_DigestSignFinal(EVP_MD_CTX*, unsigned char*, std::size_t*) { return 1; }
inline int EVP_Digest(
    const void*, std::size_t, unsigned char*, unsigned int* size,
    const EVP_MD*, void*) {
    *size = 32;
    return 1;
}
