// Compile-only OpenSSL crypto API subset.
#pragma once

#include <cstddef>

inline void OPENSSL_cleanse(void*, std::size_t) {}
inline int CRYPTO_memcmp(const void*, const void*, std::size_t) { return 0; }
