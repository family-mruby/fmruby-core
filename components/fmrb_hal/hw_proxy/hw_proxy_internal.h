#pragma once

// Internal header for hw_proxy parameter structs.
// Used by hw_proxy execute implementations and fmrb_hal platform files.
// Not part of the public API.

#include "hw_proxy.h"
#include "fmrb_hal_file.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----- FILE params -----

typedef struct {
    const char *path;
    uint32_t flags;
    fmrb_file_t *out_handle;
} hw_proxy_file_open_params_t;

typedef struct {
    fmrb_file_t handle;
} hw_proxy_file_close_params_t;

typedef struct {
    fmrb_file_t handle;
    void *buf;
    size_t size;
    size_t *out_read;
} hw_proxy_file_read_params_t;

typedef struct {
    fmrb_file_t handle;
    const void *buf;
    size_t size;
    size_t *out_written;
} hw_proxy_file_write_params_t;

typedef struct {
    fmrb_file_t handle;
    int32_t offset;
    uint32_t whence;
} hw_proxy_file_seek_params_t;

typedef struct {
    fmrb_file_t handle;
    uint32_t *position;
} hw_proxy_file_tell_params_t;

typedef struct {
    fmrb_file_t handle;
    uint32_t *size;
} hw_proxy_file_size_params_t;

typedef struct {
    const char *path;
    fmrb_file_info_t *info;
} hw_proxy_file_stat_params_t;

// ----- DIR params -----

typedef struct {
    const char *path;
    void *out_dir;       // DIR* result (opaque)
} hw_proxy_dir_open_params_t;

typedef struct {
    void *dir;           // DIR* handle
    const char **out_name; // pointer to dirent name (static, valid until next readdir)
} hw_proxy_dir_read_params_t;

typedef struct {
    void *dir;           // DIR* handle
} hw_proxy_dir_close_params_t;

typedef struct {
    const char *path;
    int *out_is_dir;     // 1 if directory, 0 otherwise
} hw_proxy_dir_stat_params_t;

// ----- GPIO params -----

typedef struct {
    int pin;
} hw_proxy_gpio_reset_params_t;

typedef struct {
    int pin;
    int mode;
    int pull;
} hw_proxy_gpio_config_params_t;

typedef struct {
    int pin;
    int32_t *out_level;
} hw_proxy_gpio_read_params_t;

typedef struct {
    int pin;
    uint32_t level;
} hw_proxy_gpio_write_params_t;

typedef struct {
    int pin;
    int pull;
} hw_proxy_gpio_pull_params_t;

// ----- I2C params -----

typedef struct {
    int unit;
    uint32_t freq;
    int8_t sda;
    int8_t scl;
} hw_proxy_i2c_init_params_t;

typedef struct {
    int unit;
    uint8_t addr;
    uint8_t *buf;
    size_t len;
    bool nostop;
    uint32_t timeout_us;
} hw_proxy_i2c_rw_params_t;

// ----- RMT params -----

typedef struct {
    uint32_t gpio;
    uint32_t t0h_ns;
    uint32_t t0l_ns;
    uint32_t t1h_ns;
    uint32_t t1l_ns;
    uint32_t reset_ns;
} hw_proxy_rmt_init_params_t;

typedef struct {
    uint8_t *buffer;
    uint32_t nbytes;
} hw_proxy_rmt_write_params_t;

// ----- Execute function declarations (implemented in hw_proxy_*.c) -----

void hw_proxy_file_execute(hw_proxy_request_t *req);
void hw_proxy_gpio_execute(hw_proxy_request_t *req);
void hw_proxy_i2c_execute(hw_proxy_request_t *req);
void hw_proxy_rmt_execute(hw_proxy_request_t *req);

// ----- Resource release (called on app termination) -----

void hw_proxy_i2c_release(hw_proxy_task_handle_t owner);
void hw_proxy_rmt_release(hw_proxy_task_handle_t owner);

#ifdef __cplusplus
}
#endif
