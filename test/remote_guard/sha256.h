#pragma once

#include <stddef.h>
#include <stdint.h>

/** SHA-256 for the host tests (sha256.c). */
void test_sha256(const void *in, size_t len, uint8_t out[32]);
