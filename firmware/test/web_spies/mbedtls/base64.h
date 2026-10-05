#pragma once
#include <cstddef>
// Link the real host libmbedcrypto implementation; no crypto behavior is mocked.
extern "C" int mbedtls_base64_decode(unsigned char*, size_t, size_t*, const unsigned char*, size_t);
extern "C" int mbedtls_base64_encode(unsigned char*, size_t, size_t*, const unsigned char*, size_t);
