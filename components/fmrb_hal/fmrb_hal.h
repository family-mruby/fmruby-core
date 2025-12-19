#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Platform detection
#ifdef CONFIG_IDF_TARGET_LINUX
#define FMRB_PLATFORM_LINUX 1
#else
#define FMRB_PLATFORM_ESP32 1
#endif

// Include platform-specific headers only when building with ESP-IDF
#include "fmrb_hal_esp.h"

// HAL initialization
fmrb_err_t fmrb_hal_init(void);
void fmrb_hal_deinit(void);

// Include sub-modules
#include "fmrb_hal_time.h"
#include "fmrb_hal_gpio.h"
#include "fmrb_hal_spi.h"
#include "fmrb_hal_link.h"
#include "fmrb_hal_file.h"
#include "fmrb_hal_uart.h"

#ifdef __cplusplus
}
#endif
