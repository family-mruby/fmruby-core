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
#include "fmrb_hal_rtos.h"

// Time types
typedef uint64_t fmrb_time_t;  // microseconds since boot

// GPIO types
typedef int fmrb_gpio_num_t;
typedef enum {
    FMRB_GPIO_MODE_INPUT,
    FMRB_GPIO_MODE_OUTPUT,
    FMRB_GPIO_MODE_OUTPUT_OD  // Open drain
} fmrb_gpio_mode_t;

typedef enum {
    FMRB_GPIO_PULL_NONE,
    FMRB_GPIO_PULL_UP,
    FMRB_GPIO_PULL_DOWN
} fmrb_gpio_pull_mode_t;

// SPI types
typedef void* fmrb_spi_handle_t;
typedef struct {
    int mosi_pin;
    int miso_pin;
    int sclk_pin;
    int cs_pin;
    int frequency;
} fmrb_spi_config_t;

// Link layer communication types
typedef enum {
    FMRB_LINK_GRAPHICS = 0,
    FMRB_LINK_AUDIO = 1,
    FMRB_LINK_MAX_CHANNELS
} fmrb_link_channel_t;

typedef struct {
    uint8_t *data;
    size_t size;
} fmrb_link_message_t;

// Function pointer types
typedef void (*fmrb_link_callback_t)(fmrb_link_channel_t channel, const fmrb_link_message_t *msg, void *user_data);

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
#include "fmrb_msg.h"

#ifdef __cplusplus
}
#endif
