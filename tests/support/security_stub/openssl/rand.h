// Compile-only OpenSSL RAND API subset.
#pragma once

inline int RAND_bytes(unsigned char*, int) { return 1; }
