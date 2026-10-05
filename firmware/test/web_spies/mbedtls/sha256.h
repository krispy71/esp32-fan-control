#pragma once
#include <cstddef>
extern "C" int mbedtls_sha256_ret(const unsigned char*, size_t, unsigned char[32], int);
