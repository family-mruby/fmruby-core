#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 8x8 Japanese bitmap font (Misaki Gothic) in u8g2 binary format.
// Raw data array; consumed via lgfx::v1::U8g2font (see lgfx_misaki.cpp).
extern const uint8_t lgfx_misaki_8[];

#ifdef __cplusplus
}
#endif
