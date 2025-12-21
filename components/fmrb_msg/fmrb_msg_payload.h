#pragma once

#include <stdint.h>
#include "fmrb_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file fmrb_msg_payload.h
 * @brief Message payload definitions for inter-task communication
 *
 * This header defines the structure of message payloads used in
 * FMRB_MSG_TYPE_APP_CONTROL messages.
 */

// App control message subtypes
#define FMRB_APP_CTRL_SPAWN   1  // Spawn application request
#define FMRB_APP_CTRL_KILL    2  // Kill application request
#define FMRB_APP_CTRL_SUSPEND 3  // Suspend application
#define FMRB_APP_CTRL_RESUME  4  // Resume application

/**
 * @brief Spawn app request payload
 *
 * Sent from SystemGUI or other apps to Kernel to request spawning
 * a new application by name.
 *
 * Size: 128 bytes (expanded to support longer file paths)
 */
typedef struct {
    uint8_t subtype;                     // FMRB_APP_CTRL_SPAWN
    char app_name[FMRB_MAX_PATH_LEN];  // Application name or file path (null-terminated)
    uint8_t reserved[7];                 // Reserved for future parameters
} __attribute__((packed)) fmrb_app_ctrl_spawn_req_t;

#ifdef __cplusplus
}
#endif
