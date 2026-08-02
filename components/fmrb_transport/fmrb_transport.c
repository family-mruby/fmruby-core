#include "fmrb_transport.h"
#include "fmrb.h"
#include "fmrb_link_protocol.h"
#include "fmrb_transport_fragment.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"
#include "fmrb_debug.h"
#include "fmrb_app.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

// Override msgpack sbuffer initial allocation before including msgpack.h.
// Default is 8192 bytes; a single message is at most FMRB_LINK_FRAME_MAX_DATA
// payload + msgpack overhead (~10 bytes). Use 2x frame data size as margin.
#define MSGPACK_SBUFFER_INIT_SIZE  (FMRB_LINK_FRAME_MAX_DATA * 2)

#include <msgpack.h>

#define MAX_CALLBACKS 16
#define MAX_PENDING_MESSAGES 8
#define MAX_SYNC_REQUESTS 4

typedef struct {
    uint8_t msg_type;
    fmrb_transport_callback_t callback;
    void *user_data;
} callback_entry_t;

typedef struct {
    uint16_t sequence;
    uint8_t link_type;
    uint8_t sub_cmd;
    uint8_t *payload;
    uint32_t payload_len;
    fmrb_time_t sent_time;
    uint8_t retry_count;
} pending_message_t;

// Async/sync request tracking
typedef struct {
    uint16_t sequence;
    bool active;
    // Async callback (if set, invoked in host_task context on response)
    fmrb_transport_response_cb_t callback;
    void *callback_user_data;
    fmrb_tick_t timeout_tick;    // Expiry tick (0 = no timeout)
    // Sync fields (used when callback == NULL)
    bool response_received;
    uint8_t response_status;
    uint8_t *response_payload;
    uint32_t response_len;
    uint32_t response_max_len;
    fmrb_semaphore_t wait_sem;
} sync_request_t;

typedef struct {
    fmrb_transport_config_t config;
    uint16_t next_sequence;

    callback_entry_t callbacks[MAX_CALLBACKS];
    int callback_count;

    pending_message_t pending_messages[MAX_PENDING_MESSAGES];
    int pending_count;

    sync_request_t sync_requests[MAX_SYNC_REQUESTS];
    fmrb_semaphore_t sync_mutex;  // Mutex (semaphore) for protecting sync_requests array

    fmrb_fragment_manager_t fragment_manager;  // Fragment manager for chunking

    // Throughput statistics (reset every STATS_INTERVAL_MS)
    uint32_t stats_tx_bytes;
    uint32_t stats_tx_msgs;
    uint32_t stats_rx_msgs;
    fmrb_time_t stats_last_us;

    bool initialized;
} transport_context_t;

#define TRANSPORT_STATS_INTERVAL_MS  5000

static transport_context_t g_tranport_context;

static const char *TAG = "fmrb_transport";

// Forward declarations
static void handle_received_message(transport_context_t *ctx, uint8_t type, uint8_t seq,
                                    uint8_t sub_cmd, const uint8_t *payload, uint32_t payload_len);

fmrb_err_t fmrb_transport_init(const fmrb_transport_config_t *config) {
    if (!config) {
        return FMRB_ERR_INVALID_PARAM;
    }

    transport_context_t *ctx = &g_tranport_context;

    if(ctx->initialized)
    {
        FMRB_LOGI(TAG,"already initialized");
        return FMRB_ERR_INVALID_STATE;
    }

    memset(ctx, 0, sizeof(transport_context_t));
    ctx->config = *config;
    ctx->next_sequence = 1;

    // Initialize sync request tracking
    ctx->sync_mutex = fmrb_semaphore_create_mutex();
    if (!ctx->sync_mutex) {
        return FMRB_ERR_NO_MEMORY;
    }

    for (int i = 0; i < MAX_SYNC_REQUESTS; i++) {
        ctx->sync_requests[i].active = false;
        ctx->sync_requests[i].wait_sem = fmrb_semaphore_create_binary();
        if (!ctx->sync_requests[i].wait_sem) {
            // Cleanup already created semaphores
            for (int j = 0; j < i; j++) {
                fmrb_semaphore_delete(ctx->sync_requests[j].wait_sem);
            }
            fmrb_semaphore_delete(ctx->sync_mutex);
            return FMRB_ERR_NO_MEMORY;
        }
    }

    // Initialize fragment manager
    fmrb_fragment_manager_init(&ctx->fragment_manager);

    ctx->initialized = true;

    FMRB_LOGI(TAG,"initialized");
    return FMRB_OK;
}

fmrb_err_t fmrb_transport_deinit(void) {
    transport_context_t *ctx = &g_tranport_context;

    // Free pending messages
    for (int i = 0; i < ctx->pending_count; i++) {
        if (ctx->pending_messages[i].payload) {
            fmrb_sys_free(ctx->pending_messages[i].payload);
        }
    }

    // Cleanup sync requests
    for (int i = 0; i < MAX_SYNC_REQUESTS; i++) {
        if (ctx->sync_requests[i].wait_sem) {
            fmrb_semaphore_delete(ctx->sync_requests[i].wait_sem);
        }
    }

    if (ctx->sync_mutex) {
        fmrb_semaphore_delete(ctx->sync_mutex);
    }

    // Cleanup fragment manager
    fmrb_fragment_manager_cleanup(&ctx->fragment_manager);

    ctx->initialized = false;
    return FMRB_OK;
}

static fmrb_err_t send_raw_message(uint8_t link_type, uint8_t seq, uint8_t sub_cmd,
                                   const uint8_t *payload, uint32_t payload_len,
                                   uint32_t timeout_ms) {
    transport_context_t *ctx = &g_tranport_context;

    // Serialize with msgpack as per IPC_spec.md:
    // 1. Pack frame_hdr + sub_cmd + payload with msgpack
    // 2. HAL layer will add CRC32 and COBS encode

    // Create msgpack buffer
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    // Try non-chunked path first: encode, then check if it fits in one SPI frame
    msgpack_pack_array(&pk, 4);
    msgpack_pack_uint8(&pk, link_type);
    msgpack_pack_uint8(&pk, seq);
    msgpack_pack_uint8(&pk, sub_cmd);

    if (payload && payload_len > 0) {
        msgpack_pack_bin(&pk, payload_len);
        msgpack_pack_bin_body(&pk, payload, payload_len);
    } else {
        msgpack_pack_nil(&pk);
    }

    // COBS worst case: input_len + ceil(input_len/254) + 1 (terminator)
    size_t cobs_worst = sbuf.size + (sbuf.size / 254) + 2;

    if (cobs_worst <= FMRB_LINK_FRAME_MAX_DATA) {
        // Fits in a single SPI frame: send directly
        fmrb_link_message_t hal_msg = {
            .data = (uint8_t*)sbuf.data,
            .size = sbuf.size
        };

        fmrb_link_channel_t hal_channel = FMRB_LINK_CHANNEL_DEFAULT;
        fmrb_err_t ret = fmrb_hal_link_send_noack(hal_channel, &hal_msg, 1, timeout_ms);

        if (ret == FMRB_OK) {
            ctx->stats_tx_bytes += sbuf.size;
            ctx->stats_tx_msgs++;
        } else if (link_type != FMRB_LINK_TYPE_GRAPHICS) {
            FMRB_LOGE(TAG, "HAL send failed: ret=%d, type=%d, sub_cmd=0x%02X", ret, link_type, sub_cmd);
        }

        msgpack_sbuffer_destroy(&sbuf);
        return ret;
    }

    // Does not fit: use fragmentation
    msgpack_sbuffer_destroy(&sbuf);
    {
        // Fragmentation path: split into multiple chunks
        FMRB_LOGI(TAG, "Large message (%u bytes), using fragmentation", payload_len);

        // Allocate chunk ID
        uint8_t chunk_id = fmrb_fragment_alloc_chunk_id(&ctx->fragment_manager);

        // Initialize send context
        fmrb_fragment_send_ctx_t send_ctx;
        fmrb_fragment_init_send_ctx(&send_ctx, payload, payload_len,
                                     link_type | FMRB_LINK_FLAG_CHUNKED, seq, chunk_id);

        fmrb_err_t ret = FMRB_OK;
        fmrb_link_channel_t hal_channel = FMRB_LINK_CHANNEL_DEFAULT;

        // Send all chunks
        while (true) {
            fmrb_link_chunk_info_t chunk_info;
            const uint8_t *chunk_data;
            uint32_t chunk_len;

            ret = fmrb_fragment_get_next_chunk(&send_ctx, &chunk_info, &chunk_data, &chunk_len);
            if (ret != FMRB_OK) {
                FMRB_LOGE(TAG, "Failed to get next chunk: %d", ret);
                break;
            }

            // Pack chunk message: [type|CHUNKED, seq, sub_cmd, chunk_info, chunk_data]
            msgpack_sbuffer chunk_sbuf;
            msgpack_sbuffer_init(&chunk_sbuf);
            msgpack_packer chunk_pk;
            msgpack_packer_init(&chunk_pk, &chunk_sbuf, msgpack_sbuffer_write);

            msgpack_pack_array(&chunk_pk, 5);
            msgpack_pack_uint8(&chunk_pk, link_type | FMRB_LINK_FLAG_CHUNKED);
            msgpack_pack_uint8(&chunk_pk, seq);
            msgpack_pack_uint8(&chunk_pk, sub_cmd);

            // Pack chunk_info struct as binary
            msgpack_pack_bin(&chunk_pk, sizeof(fmrb_link_chunk_info_t));
            msgpack_pack_bin_body(&chunk_pk, &chunk_info, sizeof(fmrb_link_chunk_info_t));

            // Pack chunk payload
            msgpack_pack_bin(&chunk_pk, chunk_len);
            msgpack_pack_bin_body(&chunk_pk, chunk_data, chunk_len);

            // Send chunk
            fmrb_link_message_t hal_msg = {
                .data = (uint8_t*)chunk_sbuf.data,
                .size = chunk_sbuf.size
            };

            ret = fmrb_hal_link_send_noack(hal_channel, &hal_msg, 1, timeout_ms);

            msgpack_sbuffer_destroy(&chunk_sbuf);

            if (ret != FMRB_OK) {
                FMRB_LOGE(TAG, "Failed to send chunk %u/%u: %d",
                          chunk_info.offset / FMRB_LINK_FRAG_MAX_CHUNK_PAYLOAD,
                          fmrb_fragment_calculate_num_chunks(payload_len), ret);
                break;
            }

            FMRB_LOGD(TAG, "Sent chunk: offset=%u, len=%u, flags=0x%02X",
                      chunk_info.offset, chunk_len, chunk_info.flags);

            // Check if this was the last chunk
            if (chunk_info.flags & FMRB_LINK_CHUNK_FL_END) {
                ret = FMRB_OK;
                break;
            }

            // TODO: Implement sliding window flow control and ACK handling here
            // For now, send all chunks without waiting for ACKs
        }

        return ret;
    }
}

// TODO: Used for sliding window flow control (not yet implemented)
// static fmrb_err_t add_pending_message(transport_context_t *ctx, uint16_t sequence,
//                                       uint8_t link_type, uint8_t sub_cmd,
//                                       const uint8_t *payload, uint32_t payload_len) {
//     if (ctx->pending_count >= MAX_PENDING_MESSAGES) {
//         return FMRB_ERR_FAILED;
//     }
//
//     pending_message_t *pending = &ctx->pending_messages[ctx->pending_count];
//     pending->sequence = sequence;
//     pending->link_type = link_type;
//     pending->sub_cmd = sub_cmd;
//     pending->payload_len = payload_len;
//     pending->sent_time = fmrb_hal_time_get_us();
//     pending->retry_count = 0;
//
//     if (payload_len > 0 && payload) {
//         pending->payload = fmrb_sys_malloc(payload_len);
//         if (!pending->payload) {
//             return FMRB_ERR_NO_MEMORY;
//         }
//         memcpy(pending->payload, payload, payload_len);
//     } else {
//         pending->payload = NULL;
//     }
//
//     ctx->pending_count++;
//     return FMRB_OK;
// }

fmrb_err_t fmrb_transport_send(uint8_t link_type,
                                    uint8_t sub_cmd,
                                    const uint8_t *payload,
                                    uint32_t payload_len,
                                    int32_t timeout_ms) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Resolve timeout: <0 = default, 0 = no timeout, >0 = specified value
    uint32_t effective_timeout;
    if (timeout_ms < 0) {
        effective_timeout = ctx->config.timeout_ms;
    } else if (timeout_ms == 0) {
        effective_timeout = UINT32_MAX;
    } else {
        effective_timeout = (uint32_t)timeout_ms;
    }

    // Drain pending ACKs before sending to prevent ack_recv_queue overflow
    // during rapid consecutive sends (e.g., command_buffer_execute)
    fmrb_transport_process();

    uint16_t sequence = ctx->next_sequence++;
    uint8_t seq = (uint8_t)(sequence & 0xFF);

    // Log graphics commands
    if (link_type == FMRB_LINK_TYPE_GRAPHICS) {
        FMRB_LOGD(TAG, "Sending GRAPHICS command: sub_cmd=0x%02X, payload_len=%u, seq=%u",
                  sub_cmd, payload_len, seq);
    }

    // Send message (HAL send is synchronous - blocks until ACK received)
    fmrb_err_t ret = send_raw_message(link_type, seq, sub_cmd, payload, payload_len, effective_timeout);
    if (ret != FMRB_OK) {
        return ret;
    }

    // Note: Fire-and-forget send. No frame-level ACK waiting.
    // App-level responses are received via fmrb_transport_process().

    return FMRB_OK;
}

fmrb_err_t fmrb_transport_send_batch(const fmrb_transport_batch_entry_t *entries,
                                      size_t entry_count,
                                      int32_t timeout_ms) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }
    if (!entries || entry_count == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Resolve timeout
    uint32_t effective_timeout;
    if (timeout_ms < 0) {
        effective_timeout = ctx->config.timeout_ms;
    } else if (timeout_ms == 0) {
        effective_timeout = UINT32_MAX;
    } else {
        effective_timeout = (uint32_t)timeout_ms;
    }

    fmrb_transport_process();

    // Msgpack-encode each entry into hal_msgs array.
    // Each entry is encoded with a single reusable sbuffer, then the encoded
    // data is copied to a compact allocation so the 8KB sbuffer can be freed
    // immediately. This avoids holding N * 8KB simultaneously.
    fmrb_link_message_t *hal_msgs = (fmrb_link_message_t *)fmrb_sys_malloc(
        entry_count * sizeof(fmrb_link_message_t));
    if (!hal_msgs) {
        return FMRB_ERR_NO_MEMORY;
    }

    msgpack_sbuffer sbuf;
    bool encode_failed = false;

    for (size_t i = 0; i < entry_count; i++) {
        msgpack_sbuffer_init(&sbuf);
        msgpack_packer pk;
        msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

        uint16_t sequence = ctx->next_sequence++;
        uint8_t seq = (uint8_t)(sequence & 0xFF);

        msgpack_pack_array(&pk, 4);
        msgpack_pack_uint8(&pk, entries[i].link_type);
        msgpack_pack_uint8(&pk, seq);
        msgpack_pack_uint8(&pk, entries[i].sub_cmd);

        if (entries[i].payload && entries[i].payload_len > 0) {
            msgpack_pack_bin(&pk, entries[i].payload_len);
            msgpack_pack_bin_body(&pk, entries[i].payload, entries[i].payload_len);
        } else {
            msgpack_pack_nil(&pk);
        }

        if (sbuf.size == 0) {
            FMRB_LOGE(TAG, "Batch entry %zu/%zu msgpack encode failed", i, entry_count);
            hal_msgs[i].data = NULL;
            hal_msgs[i].size = 0;
            encode_failed = true;
            msgpack_sbuffer_destroy(&sbuf);
            continue;
        }

        // Copy encoded data to a compact buffer and release the 8KB sbuffer
        uint8_t *msg_data = (uint8_t *)fmrb_sys_malloc(sbuf.size);
        if (!msg_data) {
            FMRB_LOGE(TAG, "Batch entry %zu/%zu copy alloc failed (%zu bytes)", i, entry_count, sbuf.size);
            hal_msgs[i].data = NULL;
            hal_msgs[i].size = 0;
            encode_failed = true;
            msgpack_sbuffer_destroy(&sbuf);
            continue;
        }
        memcpy(msg_data, sbuf.data, sbuf.size);
        hal_msgs[i].data = msg_data;
        hal_msgs[i].size = sbuf.size;

        msgpack_sbuffer_destroy(&sbuf);
    }

    // Build a compact array without gaps (skip failed entries)
    size_t valid_count = 0;
    for (size_t i = 0; i < entry_count; i++) {
        if (hal_msgs[i].data && hal_msgs[i].size > 0) {
            if (valid_count != i) {
                hal_msgs[valid_count] = hal_msgs[i];
            }
            valid_count++;
        }
    }

    if (encode_failed) {
        FMRB_LOGW(TAG, "Batch: %zu/%zu entries encoded successfully", valid_count, entry_count);
    }

    // Send batch via HAL (fire-and-forget)
    fmrb_link_channel_t hal_channel = FMRB_LINK_CHANNEL_DEFAULT;
    fmrb_err_t ret = fmrb_hal_link_send_noack(hal_channel, hal_msgs, valid_count, effective_timeout);

    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Batch send failed: ret=%d, count=%zu", ret, valid_count);
    } else {
        for (size_t i = 0; i < valid_count; i++) {
            ctx->stats_tx_bytes += hal_msgs[i].size;
        }
        ctx->stats_tx_msgs += valid_count;
        FMRB_LOGD(TAG, "Batch sent: %zu messages", valid_count);
    }

    // Free all compact buffers
    for (size_t i = 0; i < valid_count; i++) {
        if (hal_msgs[i].data) {
            fmrb_sys_free(hal_msgs[i].data);
        }
    }
    fmrb_sys_free(hal_msgs);

    return ret;
}

fmrb_err_t fmrb_transport_send_async(uint8_t link_type,
                                      uint8_t sub_cmd,
                                      const uint8_t *payload,
                                      uint32_t payload_len,
                                      fmrb_transport_response_cb_t callback,
                                      void *user_data,
                                      uint32_t timeout_ms) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized || !callback) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Allocate request slot
    fmrb_semaphore_take(ctx->sync_mutex, FMRB_TICK_MAX);

    int slot = -1;
    for (int i = 0; i < MAX_SYNC_REQUESTS; i++) {
        if (!ctx->sync_requests[i].active) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        fmrb_semaphore_give(ctx->sync_mutex);
        FMRB_LOGE(TAG, "No available request slots for send_async");
        return FMRB_ERR_BUSY;
    }

    uint16_t sequence = ctx->next_sequence++;
    uint8_t seq = (uint8_t)(sequence & 0xFF);

    sync_request_t *req = &ctx->sync_requests[slot];
    req->sequence = sequence;
    req->active = true;
    req->callback = callback;
    req->callback_user_data = user_data;
    req->timeout_tick = (timeout_ms > 0) ?
        fmrb_task_get_tick_count() + FMRB_MS_TO_TICKS(timeout_ms) : 0;
    req->response_received = false;
    req->response_payload = NULL;
    req->response_max_len = 0;
    req->response_len = 0;

    fmrb_semaphore_give(ctx->sync_mutex);

    // Send with ACK_REQUIRED
    fmrb_err_t ret = send_raw_message(link_type | FMRB_LINK_FLAG_ACK_REQUIRED,
                                       seq, sub_cmd, payload, payload_len, timeout_ms);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "send_async: send_raw_message failed: %d (seq=%u)", ret, sequence);
        fmrb_semaphore_take(ctx->sync_mutex, FMRB_TICK_MAX);
        req->active = false;
        fmrb_semaphore_give(ctx->sync_mutex);
        return ret;
    }

    return FMRB_OK;
}

// Sync wrapper callback context
typedef struct {
    fmrb_semaphore_t done;
    uint8_t status;
    uint8_t *buf;
    uint32_t buf_max;
    uint32_t len;
} sync_wrapper_t;

static void sync_response_callback(uint8_t status, const uint8_t *payload,
                                    uint32_t payload_len, void *user_data) {
    sync_wrapper_t *w = (sync_wrapper_t *)user_data;
    w->status = status;
    if (payload && payload_len > 0 && w->buf && payload_len <= w->buf_max) {
        memcpy(w->buf, payload, payload_len);
        w->len = payload_len;
    }
    fmrb_semaphore_give(w->done);
}

fmrb_err_t fmrb_transport_send_sync(uint8_t link_type,
                                         uint8_t sub_cmd,
                                         const uint8_t *payload,
                                         uint32_t payload_len,
                                         uint8_t *response_payload,
                                         uint32_t *response_len,
                                         uint32_t timeout_ms) {
    sync_wrapper_t wrapper = {
        .done = fmrb_semaphore_create_binary(),
        .status = 0xFF,
        .buf = response_payload,
        .buf_max = (response_payload && response_len) ? *response_len : 0,
        .len = 0,
    };
    if (!wrapper.done) {
        return FMRB_ERR_NO_MEMORY;
    }

    fmrb_err_t ret = fmrb_transport_send_async(link_type, sub_cmd, payload, payload_len,
                                                sync_response_callback, &wrapper, timeout_ms);
    if (ret != FMRB_OK) {
        fmrb_semaphore_delete(wrapper.done);
        return ret;
    }

    // Block calling task until callback signals or timeout
    fmrb_tick_t ticks = (timeout_ms == UINT32_MAX) ? FMRB_TICK_MAX : FMRB_MS_TO_TICKS(timeout_ms);
    FMRB_LOGD(TAG, "send_sync: waiting for response (timeout_ms=%u)", (unsigned)timeout_ms);
        // The reply context is on this stack; hold off a forced delete.
    fmrb_app_sync_io_begin();
    fmrb_base_type_t wait_result = fmrb_semaphore_take(wrapper.done, ticks);
    fmrb_app_sync_io_end();
    fmrb_semaphore_delete(wrapper.done);

    if (wait_result != FMRB_TRUE) {
        FMRB_LOGW(TAG, "send_sync: timeout");
        return FMRB_ERR_TIMEOUT;
    }

    if (response_len) {
        *response_len = wrapper.len;
    }

    if (wrapper.status != 0) {
        FMRB_LOGW(TAG, "send_sync: error response status=%u", wrapper.status);
        return FMRB_ERR_FAILED;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_transport_register_callback(uint8_t msg_type,
                                                 fmrb_transport_callback_t callback,
                                                 void *user_data) {
    if (!callback) {
        return FMRB_ERR_INVALID_PARAM;
    }

    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    if (ctx->callback_count >= MAX_CALLBACKS) {
        return FMRB_ERR_BUSY;
    }

    callback_entry_t *entry = &ctx->callbacks[ctx->callback_count];
    entry->msg_type = msg_type;
    entry->callback = callback;
    entry->user_data = user_data;

    ctx->callback_count++;
    return FMRB_OK;
}

fmrb_err_t fmrb_transport_unregister_callback(uint8_t msg_type) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Find and remove callback
    for (int i = 0; i < ctx->callback_count; i++) {
        if (ctx->callbacks[i].msg_type == msg_type) {
            // Shift remaining callbacks
            for (int j = i; j < ctx->callback_count - 1; j++) {
                ctx->callbacks[j] = ctx->callbacks[j + 1];
            }
            ctx->callback_count--;
            return FMRB_OK;
        }
    }

    return FMRB_ERR_NOT_FOUND;
}

static void handle_received_message(transport_context_t *ctx, uint8_t type, uint8_t seq,
                                    uint8_t sub_cmd, const uint8_t *payload, uint32_t payload_len) {
    // Handle ACK/NACK messages
    if (sub_cmd == FMRB_LINK_RESPONSE_MSG_ACK || sub_cmd == FMRB_LINK_RESPONSE_MSG_NACK) {
        uint8_t response_status = (sub_cmd == FMRB_LINK_RESPONSE_MSG_ACK) ? 0 : 1;
        FMRB_LOGD(TAG, "Received %s: type=%u seq=%u payload_len=%u",
                  (sub_cmd == FMRB_LINK_RESPONSE_MSG_ACK) ? "ACK" : "NACK",
                  type, seq, payload_len);
        // Note: seq is 8-bit from wire protocol, but internal sequence is 16-bit
        // We need to match against pending_messages[].sequence (uint16_t) using only lower 8 bits
        uint8_t seq_8bit = seq;
        const uint8_t *response_data = payload;
        uint32_t response_data_len = payload_len;

        // Check if this is a response to a pending request (async or sync)
        fmrb_semaphore_take(ctx->sync_mutex, FMRB_TICK_MAX);
        for (int i = 0; i < MAX_SYNC_REQUESTS; i++) {
            if (ctx->sync_requests[i].active && (ctx->sync_requests[i].sequence & 0xFF) == seq_8bit) {
                sync_request_t *req = &ctx->sync_requests[i];

                if (req->callback) {
                    // Async path: invoke callback in current (host_task) context
                    fmrb_transport_response_cb_t cb = req->callback;
                    void *ud = req->callback_user_data;
                    req->active = false;
                    fmrb_semaphore_give(ctx->sync_mutex);

                    FMRB_LOGD(TAG, "ACK matched async request: seq=%u, resp_len=%u",
                              seq_8bit, response_data_len);
                    cb(response_status, response_data, response_data_len, ud);
                } else {
                    // Sync path: fill buffer and signal semaphore
                    req->response_received = true;
                    req->response_status = response_status;

                    if (response_data && response_data_len > 0 && req->response_payload) {
                        uint32_t copy_len = (response_data_len < req->response_max_len) ? response_data_len : req->response_max_len;
                        memcpy(req->response_payload, response_data, copy_len);
                        req->response_len = copy_len;
                    }

                    FMRB_LOGD(TAG, "ACK matched sync request: seq=%u, resp_len=%u",
                              seq_8bit, req->response_len);
                    fmrb_semaphore_give(req->wait_sem);
                    fmrb_semaphore_give(ctx->sync_mutex);
                }

                // Remove from pending list for retransmit tracking
                for (int j = 0; j < ctx->pending_count; j++) {
                    if ((ctx->pending_messages[j].sequence & 0xFF) == seq_8bit) {
                        if (ctx->pending_messages[j].payload) {
                            fmrb_sys_free(ctx->pending_messages[j].payload);
                        }
                        for (int k = j; k < ctx->pending_count - 1; k++) {
                            ctx->pending_messages[k] = ctx->pending_messages[k + 1];
                        }
                        ctx->pending_count--;
                        break;
                    }
                }
                return;
            }
        }
        fmrb_semaphore_give(ctx->sync_mutex);

        // Not a sync request, remove from pending list (async send ACK)
        for (int i = 0; i < ctx->pending_count; i++) {
            if ((ctx->pending_messages[i].sequence & 0xFF) == seq_8bit) {
                FMRB_LOGD(TAG, "ACK cleared pending message: seq=%u (retries=%u)",
                          seq_8bit, ctx->pending_messages[i].retry_count);
                if (ctx->pending_messages[i].payload) {
                    fmrb_sys_free(ctx->pending_messages[i].payload);
                }

                // Shift remaining messages
                for (int j = i; j < ctx->pending_count - 1; j++) {
                    ctx->pending_messages[j] = ctx->pending_messages[j + 1];
                }
                ctx->pending_count--;
                break;
            }
        }
        return;
    }

    // Find callback for message type
    for (int i = 0; i < ctx->callback_count; i++) {
        if (ctx->callbacks[i].msg_type == sub_cmd) {
            ctx->callbacks[i].callback(type, seq, sub_cmd, payload, payload_len, ctx->callbacks[i].user_data);
            break;
        }
    }

    // Send ACK
    fmrb_link_ack_t ack = {
        .original_sequence = seq,
        .status = 0
    };

    uint16_t ack_sequence = ctx->next_sequence++;
    uint8_t ack_seq = (uint8_t)(ack_sequence & 0xFF);
    send_raw_message(FMRB_LINK_TYPE_CONTROL, ack_seq, FMRB_LINK_RESPONSE_MSG_ACK, (uint8_t*)&ack, sizeof(ack), ctx->config.timeout_ms);
}

fmrb_err_t fmrb_transport_process(void) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Check for incoming messages (loop to process all buffered frames)
    fmrb_link_message_t hal_msg;
    int processed_count = 0;
    const int max_process_per_call = 10;  // Prevent infinite loop

    while (processed_count < max_process_per_call &&
           fmrb_hal_link_receive(FMRB_LINK_CHANNEL_DEFAULT, &hal_msg, 0) == FMRB_OK) {
        processed_count++;
        FMRB_LOGD(TAG, "Processing frame %d (size=%u)", processed_count, hal_msg.size);

        // Decode msgpack message: [type, seq, sub_cmd, payload]
        msgpack_unpacked msg;
        msgpack_unpacked_init(&msg);
        msgpack_unpack_return ret = msgpack_unpack_next(&msg, (const char*)hal_msg.data, hal_msg.size, NULL);

        if (ret == MSGPACK_UNPACK_SUCCESS && msg.data.type == MSGPACK_OBJECT_ARRAY && msg.data.via.array.size == 4) {
            uint8_t type = 0, seq = 0, sub_cmd = 0;
            const uint8_t *payload = NULL;
            uint32_t payload_len = 0;

            // Extract type
            if (msg.data.via.array.ptr[0].type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                type = (uint8_t)msg.data.via.array.ptr[0].via.u64;
            }

            // Extract seq
            if (msg.data.via.array.ptr[1].type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                seq = (uint8_t)msg.data.via.array.ptr[1].via.u64;
            }

            // Extract sub_cmd
            if (msg.data.via.array.ptr[2].type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                sub_cmd = (uint8_t)msg.data.via.array.ptr[2].via.u64;
            }

            // Extract payload
            if (msg.data.via.array.ptr[3].type == MSGPACK_OBJECT_BIN) {
                payload = (const uint8_t*)msg.data.via.array.ptr[3].via.bin.ptr;
                payload_len = msg.data.via.array.ptr[3].via.bin.size;
            }

            FMRB_LOGD(TAG, "Frame %d: type=%u, seq=%u, sub_cmd=%u, payload_len=%u",
                     processed_count, type, seq, sub_cmd, payload_len);
            handle_received_message(ctx, type, seq, sub_cmd, payload, payload_len);
        } else {
            FMRB_LOGE(TAG, "Frame %d: msgpack unpack failed (ret=%d)", processed_count, ret);
        }

        msgpack_unpacked_destroy(&msg);
    }

    if (processed_count > 0) {
        ctx->stats_rx_msgs += processed_count;
        FMRB_LOGD(TAG, "Processed %d frames in this call", processed_count);
    }

    // Handle retransmissions
    if (ctx->config.enable_retransmit) {
        fmrb_time_t current_time = fmrb_hal_time_get_us();

        for (int i = 0; i < ctx->pending_count; i++) {
            pending_message_t *pending = &ctx->pending_messages[i];

            if (fmrb_hal_time_is_timeout(pending->sent_time, ctx->config.timeout_ms * 1000)) {
                if (pending->retry_count < ctx->config.max_retries) {
                    // Retransmit
                    uint8_t seq = (uint8_t)(pending->sequence & 0xFF);
                    send_raw_message(pending->link_type, seq, pending->sub_cmd, pending->payload, pending->payload_len, ctx->config.timeout_ms);
                    pending->sent_time = current_time;
                    pending->retry_count++;
                } else {
                    // Max retries reached, remove from pending
                    if (pending->payload) {
                        fmrb_sys_free(pending->payload);
                    }

                    for (int j = i; j < ctx->pending_count - 1; j++) {
                        ctx->pending_messages[j] = ctx->pending_messages[j + 1];
                    }
                    ctx->pending_count--;
                    i--; // Adjust index after removal
                }
            }
        }
    }

    // Expire timed-out async request slots
    fmrb_tick_t now = fmrb_task_get_tick_count();
    fmrb_semaphore_take(ctx->sync_mutex, FMRB_TICK_MAX);
    for (int i = 0; i < MAX_SYNC_REQUESTS; i++) {
        sync_request_t *req = &ctx->sync_requests[i];
        if (req->active && req->callback && req->timeout_tick > 0 && now >= req->timeout_tick) {
            FMRB_LOGW(TAG, "Async request expired: seq=%u", req->sequence);
            req->active = false;
        }
    }
    fmrb_semaphore_give(ctx->sync_mutex);

    // Periodic throughput statistics
    fmrb_time_t now_us = fmrb_hal_time_get_us();
    if (ctx->stats_last_us == 0) {
        ctx->stats_last_us = now_us;
    } else {
        uint64_t elapsed_us = now_us - ctx->stats_last_us;
        if (elapsed_us >= (uint64_t)TRANSPORT_STATS_INTERVAL_MS * 1000) {
            uint32_t elapsed_ms = (uint32_t)(elapsed_us / 1000);
            uint32_t tx_bps = (ctx->stats_tx_bytes * 1000) / elapsed_ms;
            uint32_t tx_mps = (ctx->stats_tx_msgs * 1000) / elapsed_ms;
            uint32_t rx_mps = (ctx->stats_rx_msgs * 1000) / elapsed_ms;
            if (fmrb_debug_mode_enabled()) {
                FMRB_LOGI(TAG, "stats: tx=%lu B/s tx_msg=%lu/s rx_msg=%lu/s",
                          tx_bps, tx_mps, rx_mps);
            }
            ctx->stats_tx_bytes = 0;
            ctx->stats_tx_msgs = 0;
            ctx->stats_rx_msgs = 0;
            ctx->stats_last_us = now_us;
        }
    }

    return FMRB_OK;
}

fmrb_transport_handle_t fmrb_transport_get_handle(void) {
    return g_tranport_context.initialized ? &g_tranport_context : NULL;
}

fmrb_err_t fmrb_transport_check_version(uint32_t timeout_ms) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Prepare version request
    fmrb_control_version_req_t req = {
        .version = FMRB_LINK_VERSION
    };

    // Buffer for response
    fmrb_control_version_resp_t resp;
    uint32_t resp_len = sizeof(resp);

    FMRB_LOGI(TAG, "Checking protocol version (local=%d)", FMRB_LINK_VERSION);

    // Send version request synchronously
    // This works because host_task is running and calling fmrb_transport_process()
    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_CONTROL,
        FMRB_LINK_CONTROL_VERSION,
        (const uint8_t*)&req,
        sizeof(req),
        (uint8_t*)&resp,
        &resp_len,
        timeout_ms
    );

    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Version check failed: no response (err=%d)", ret);
        return ret;
    }

    if (resp_len != sizeof(resp)) {
        FMRB_LOGE(TAG, "Version check failed: invalid response length (%u)", resp_len);
        return FMRB_ERR_FAILED;
    }

    // Check version match
    if (resp.version != FMRB_LINK_VERSION) {
        FMRB_LOGE(TAG, "Version mismatch: local=%d, remote=%d",
                  FMRB_LINK_VERSION, resp.version);
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "Protocol version matched (version=%d)", resp.version);
    return FMRB_OK;
}

fmrb_err_t fmrb_transport_check_ga_version(uint32_t timeout_ms) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    fmrb_control_ga_version_resp_t resp;
    memset(&resp, 0, sizeof(resp));
    uint32_t resp_len = sizeof(resp);

    FMRB_LOGI(TAG, "Checking GA firmware version (expected=%s)", FMRB_GA_VERSION);

    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_CONTROL,
        FMRB_LINK_CONTROL_GA_VERSION,
        NULL,
        0,
        (uint8_t*)&resp,
        &resp_len,
        timeout_ms
    );

    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "GA version check failed: no response (err=%d)", ret);
        return ret;
    }

    if (resp_len != sizeof(resp)) {
        FMRB_LOGE(TAG, "GA version check failed: invalid response length (%u)", resp_len);
        return FMRB_ERR_FAILED;
    }

    // Ensure NUL-termination before string compare
    resp.version[FMRB_GA_VERSION_MAX_LEN - 1] = '\0';

    if (strncmp(resp.version, FMRB_GA_VERSION, FMRB_GA_VERSION_MAX_LEN) != 0) {
        FMRB_LOGE(TAG, "GA version mismatch: expected=%s, remote=%s",
                  FMRB_GA_VERSION, resp.version);
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "GA firmware version matched (version=%s)", resp.version);
    return FMRB_OK;
}