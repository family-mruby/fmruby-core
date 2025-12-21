#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file fmrb_link_types.h
 * @brief Link layer common type definitions
 *
 * This header defines common types used by the link communication layer.
 * These types are shared between HAL link implementations and higher-level
 * link components.
 */

// Link communication channel types
typedef enum {
    FMRB_LINK_GRAPHICS = 0,
    FMRB_LINK_AUDIO = 1,
    FMRB_LINK_MAX_CHANNELS
} fmrb_link_channel_t;

// Link message structure
typedef struct {
    uint8_t *data;
    size_t size;
} fmrb_link_message_t;

// Link callback function type
typedef void (*fmrb_link_callback_t)(fmrb_link_channel_t channel,
                                     const fmrb_link_message_t *msg,
                                     void *user_data);

#ifdef __cplusplus
}
#endif
