#pragma once

#include "fmrb_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FMRB_PIN_MAX 49  // ESP32-S3: GPIO 0-48

// Pin usage type
typedef enum {
    FMRB_PIN_UNUSED       = 0,
    FMRB_PIN_SYSTEM_EXCLUSIVE = 1,  // Absolutely reserved by system (USB, PSRAM, SPI, etc.)
    FMRB_PIN_USER_GPIO    = 2,  // Acquired by user app as GPIO
    FMRB_PIN_USER_I2C     = 3,  // Acquired by I2C init
    FMRB_PIN_USER_RMT     = 4,  // Acquired by RMT init
    FMRB_PIN_USER_SPI     = 5,  // Acquired by SPI
    FMRB_PIN_USER_PWM     = 6,  // Acquired by PWM
    FMRB_PIN_USER_UART    = 7,  // Acquired by UART
} fmrb_pin_usage_t;

typedef struct {
    fmrb_pin_usage_t usage;
    void *owner;   // task handle of the owner (NULL for system pins)
} fmrb_pin_status_t;

// Initialize pin manager (registers system-reserved pins)
void fmrb_pin_manager_init(void);

// Get status of a single pin
fmrb_pin_status_t fmrb_pin_manager_get_status(int pin);

// Acquire a pin for a specific usage
// Returns FMRB_OK if available, FMRB_ERR_BUSY if already in use
fmrb_err_t fmrb_pin_manager_acquire(int pin, fmrb_pin_usage_t usage, void *owner);

// Release a pin
void fmrb_pin_manager_release(int pin);

// Release all pins owned by a specific task
void fmrb_pin_manager_release_by_owner(void *owner);

// Check if a pin is available for user use
bool fmrb_pin_manager_is_available(int pin);

#ifdef __cplusplus
}
#endif
