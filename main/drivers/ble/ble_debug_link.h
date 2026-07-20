/**
 * @file ble_debug_link.h
 * @brief Glue between the BLE debug GATT service (ble_task.c) and the remote
 *        debugger's BLE transport (fmrb_debug_transport_ble.c).
 *
 * The GATT access callback runs on the NimBLE host task and must stay short,
 * so it only accumulates raw bytes and hands finished frames to the debugd
 * task through queues. Buffer ownership circulates one way:
 *
 *   free queue -> access callback fills buf[idx] -> ready queue -> transport
 *   decodes buf[idx] -> free queue
 *
 * so the two tasks never touch the same buffer at once, and the queues double
 * as the memory barrier.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "fmrb_err.h"
#include "fmrb_rtos.h"
#include "fmrb_debug_proto.h"   // FMRB_DEBUG_MAX_FRAME

/**
 * @brief Number of receive buffers cycled between the two queues.
 */
#define BLE_DBG_RX_BUF_COUNT 2

/**
 * @brief Worst-case encoded frame size.
 *
 * plain = 2 (u16 BE body length) + body + 4 (CRC32); COBS adds one code byte
 * per 254 plain bytes plus a leading one, and the frame ends with a 0x00
 * delimiter.
 */
#define BLE_DBG_MAX_ENC \
    (FMRB_DEBUG_MAX_FRAME + 2 + 4 + ((FMRB_DEBUG_MAX_FRAME + 6) / 254) + 2)

/**
 * @brief Reference to one filled receive buffer, passed through both queues.
 */
typedef struct {
    uint8_t  idx;   /**< Buffer index, 0..BLE_DBG_RX_BUF_COUNT-1. */
    uint16_t len;   /**< Encoded bytes in the buffer, delimiter excluded. */
} ble_dbg_frame_ref_t;

/**
 * @brief Register the transport's queues with the debug GATT service.
 *
 * Queues are created and owned by the transport; ble_task only keeps the
 * handles. This never touches the BLE stack, so it may be called before or
 * after BLE initialization (the P4 radio comes up on a deferred task).
 *
 * @param free_q  Queue of free buffer indices, pre-filled by the transport.
 * @param ready_q Queue the access callback posts finished frames to.
 * @return FMRB_OK, or FMRB_ERR_INVALID_PARAM if either handle is NULL.
 */
fmrb_err_t ble_debug_register(fmrb_queue_t free_q, fmrb_queue_t ready_q);

/**
 * @brief Notify an already-encoded frame (COBS + delimiter) on debug TX.
 * @param encoded Encoded frame bytes.
 * @param len     Number of bytes.
 * @return FMRB_OK on success, FMRB_ERR_INVALID_STATE if no subscriber.
 */
fmrb_err_t ble_debug_send(const uint8_t *encoded, size_t len);

/**
 * @brief Is a peer connected and subscribed to the debug TX characteristic?
 */
bool ble_debug_link_ready(void);

/**
 * @brief Raw pointer to receive buffer @p idx, for the transport to decode.
 * @return Buffer pointer, or NULL if @p idx is out of range.
 */
const uint8_t *ble_debug_rx_buf(uint8_t idx);
