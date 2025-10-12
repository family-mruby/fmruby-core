#ifndef FMRB_HAL_H
#define FMRB_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform detection
#ifdef CONFIG_IDF_TARGET_LINUX
#define FMRB_PLATFORM_LINUX 1
#else
#define FMRB_PLATFORM_ESP32 1
#endif

// Common return codes
typedef enum {
    FMRB_OK = 0,
    FMRB_ERR_INVALID_PARAM = -1,
    FMRB_ERR_NO_MEMORY = -2,
    FMRB_ERR_TIMEOUT = -3,
    FMRB_ERR_NOT_SUPPORTED = -4,
    FMRB_ERR_BUSY = -5,
    FMRB_ERR_FAILED = -6
} fmrb_err_t;

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

// IPC types
typedef enum {
    FMRB_IPC_GRAPHICS = 0,
    FMRB_IPC_AUDIO = 1,
    FMRB_IPC_MAX_CHANNELS
} fmrb_ipc_channel_t;

typedef struct {
    uint8_t *data;
    size_t size;
} fmrb_ipc_message_t;

// Function pointer types
typedef void (*fmrb_ipc_callback_t)(fmrb_ipc_channel_t channel, const fmrb_ipc_message_t *msg, void *user_data);

// HAL initialization
fmrb_err_t fmrb_hal_init(void);
void fmrb_hal_deinit(void);

// Include sub-modules
#include "fmrb_hal_time.h"
#include "fmrb_hal_gpio.h"
#include "fmrb_hal_spi.h"
#include "fmrb_hal_ipc.h"

#ifdef __cplusplus
}
#endif

#endif // FMRB_HAL_H