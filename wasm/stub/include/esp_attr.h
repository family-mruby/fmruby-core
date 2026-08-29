/* esp_attr.h stub: placement attributes mean nothing in one linear memory. */
#pragma once

#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define EXT_RAM_BSS_ATTR
#define EXT_RAM_NOINIT_ATTR
#define WORD_ALIGNED_ATTR __attribute__((aligned(4)))
#define FORCE_INLINE_ATTR static inline __attribute__((always_inline))
#define NOINLINE_ATTR __attribute__((noinline))
