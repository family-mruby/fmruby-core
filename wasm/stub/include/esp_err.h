/* Minimal esp_err.h for the wasm target: the codes the core sources compare
 * against, and esp_err_to_name for their log lines. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;

#define ESP_OK                      0
#define ESP_FAIL                    -1
#define ESP_ERR_NO_MEM              0x101
#define ESP_ERR_INVALID_ARG         0x102
#define ESP_ERR_INVALID_STATE       0x103
#define ESP_ERR_INVALID_SIZE        0x104
#define ESP_ERR_NOT_FOUND           0x105
#define ESP_ERR_NOT_SUPPORTED       0x106
#define ESP_ERR_TIMEOUT             0x107
#define ESP_ERR_INVALID_RESPONSE    0x108
#define ESP_ERR_INVALID_CRC         0x109
#define ESP_ERR_INVALID_VERSION     0x10A
#define ESP_ERR_INVALID_MAC         0x10B
#define ESP_ERR_NOT_FINISHED        0x10C

const char *esp_err_to_name(esp_err_t code);

#define ESP_ERROR_CHECK(x) do {                                   \
        esp_err_t err_rc_ = (x);                                  \
        (void)err_rc_;                                            \
    } while(0)

#ifdef __cplusplus
}
#endif
