#include "fmrb_link_transport.h"
#include "fmrb_link_protocol.h"
#include "fmrb_hal.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <msgpack.h>

#define MAX_CALLBACKS 16
#define MAX_PENDING_MESSAGES 8
#define MAX_SYNC_REQUESTS 4

// Control command definitions (should match host/common/protocol.h)
#define FMRB_CONTROL_CMD_INIT_DISPLAY 0x01

typedef struct {
    uint8_t msg_type;
    fmrb_link_transport_callback_t callback;
    void *user_data;
} callback_entry_t;

typedef struct {
    uint16_t sequence;
    uint8_t msg_type;
    uint8_t *payload;
    uint32_t payload_len;
    fmrb_time_t sent_time;
    uint8_t retry_count;
} pending_message_t;

// Synchronous request tracking
typedef struct {
    uint16_t sequence;           // Sequence number of the request
    bool active;                 // Whether this slot is in use
    bool response_received;      // Whether response has been received
    uint8_t response_status;     // Response status (0=OK, others=error)
    uint8_t *response_payload;   // Response payload buffer
    uint32_t response_len;       // Actual response length
    uint32_t response_max_len;   // Maximum response buffer size
    fmrb_semaphore_t wait_sem;   // Semaphore for waiting
} sync_request_t;

typedef struct {
    fmrb_link_transport_config_t config;
    uint16_t next_sequence;

    callback_entry_t callbacks[MAX_CALLBACKS];
    int callback_count;

    pending_message_t pending_messages[MAX_PENDING_MESSAGES];
    int pending_count;

    sync_request_t sync_requests[MAX_SYNC_REQUESTS];
    fmrb_semaphore_t sync_mutex;  // Mutex (semaphore) for protecting sync_requests array

    bool initialized;
} transport_context_t;

static transport_context_t g_tranport_context;

static const char *TAG = "fmrb_link_transport";

fmrb_err_t fmrb_link_transport_init(const fmrb_link_transport_config_t *config) {
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

    ctx->initialized = true;

    FMRB_LOGI(TAG,"initialized");
    return FMRB_OK;
}

fmrb_err_t fmrb_link_transport_deinit(void) {
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

    ctx->initialized = false;
    return FMRB_OK;
}

static fmrb_err_t send_raw_message(uint8_t link_type, uint8_t seq, uint8_t sub_cmd,
                                   const uint8_t *payload, uint32_t payload_len) {
    // Serialize with msgpack as per IPC_spec.md:
    // 1. Pack frame_hdr + sub_cmd + payload with msgpack
    // 2. HAL layer will add CRC32 and COBS encode

    // Create msgpack buffer
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    // Pack as array: [type, seq, sub_cmd, payload]
    msgpack_pack_array(&pk, 4);
    msgpack_pack_uint8(&pk, link_type);  // type (CONTROL=1, GRAPHICS=2, AUDIO=4)
    msgpack_pack_uint8(&pk, seq);        // seq
    msgpack_pack_uint8(&pk, sub_cmd);    // sub_cmd (command within type)

    // Pack payload as binary
    if (payload && payload_len > 0) {
        msgpack_pack_bin(&pk, payload_len);
        msgpack_pack_bin_body(&pk, payload, payload_len);
    } else {
        msgpack_pack_nil(&pk);
    }

    // Send via HAL (HAL will add CRC32 and COBS encode)
    fmrb_link_message_t hal_msg = {
        .data = (uint8_t*)sbuf.data,
        .size = sbuf.size
    };

    // Map link_type to HAL channel (CONTROL and GRAPHICS both use the same channel for now)
    fmrb_link_channel_t hal_channel = (link_type == FMRB_LINK_TYPE_CONTROL) ? FMRB_LINK_GRAPHICS : FMRB_LINK_GRAPHICS;
    fmrb_err_t ret = fmrb_hal_link_send(hal_channel, &hal_msg, 1000);

    msgpack_sbuffer_destroy(&sbuf);

    return ret;
}

static fmrb_err_t add_pending_message(transport_context_t *ctx, uint16_t sequence,
                                      uint8_t msg_type, const uint8_t *payload, uint32_t payload_len) {
    if (ctx->pending_count >= MAX_PENDING_MESSAGES) {
        return FMRB_ERR_FAILED;
    }

    pending_message_t *pending = &ctx->pending_messages[ctx->pending_count];
    pending->sequence = sequence;
    pending->msg_type = msg_type;
    pending->payload_len = payload_len;
    pending->sent_time = fmrb_hal_time_get_us();
    pending->retry_count = 0;

    if (payload_len > 0 && payload) {
        pending->payload = fmrb_sys_malloc(payload_len);
        if (!pending->payload) {
            return FMRB_ERR_NO_MEMORY;
        }
        memcpy(pending->payload, payload, payload_len);
    } else {
        pending->payload = NULL;
    }

    ctx->pending_count++;
    return FMRB_OK;
}

fmrb_err_t fmrb_link_transport_send(uint8_t msg_type,
                                    const uint8_t *payload,
                                    uint32_t payload_len) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    uint16_t sequence = ctx->next_sequence++;
    uint8_t seq = (uint8_t)(sequence & 0xFF);

    // Determine link_type from msg_type
    // Control commands: only FMRB_CONTROL_CMD_INIT_DISPLAY (0x01)
    // Graphics commands: everything else (Graphics and Audio ranges overlap, Audio not currently used via transport)
    uint8_t link_type;
    if (msg_type == FMRB_CONTROL_CMD_INIT_DISPLAY) {
        link_type = FMRB_LINK_TYPE_CONTROL;
    } else {
        // All other commands are graphics
        // Note: Audio commands (0x20-0x25) overlap with Graphics text commands
        // Audio subsystem uses a different communication mechanism, not this transport
        link_type = FMRB_LINK_TYPE_GRAPHICS;
    }

    // Send message
    fmrb_err_t ret = send_raw_message(link_type, seq, msg_type, payload, payload_len);
    if (ret != FMRB_OK) {
        return ret;
    }

    // Add to pending list if retransmit is enabled
    if (ctx->config.enable_retransmit) {
        add_pending_message(ctx, sequence, msg_type, payload, payload_len);
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_link_transport_send_sync(uint8_t msg_type,
                                         const uint8_t *payload,
                                         uint32_t payload_len,
                                         uint8_t *response_payload,
                                         uint32_t *response_len,
                                         uint32_t timeout_ms) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Find available sync request slot
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
        FMRB_LOGE(TAG, "No available sync request slots");
        return FMRB_ERR_BUSY;
    }

    uint16_t sequence = ctx->next_sequence++;
    uint8_t seq = (uint8_t)(sequence & 0xFF);

    // Setup sync request
    sync_request_t *req = &ctx->sync_requests[slot];
    req->sequence = sequence;
    req->active = true;
    req->response_received = false;
    req->response_payload = response_payload;
    req->response_max_len = response_payload ? (response_len ? *response_len : 0) : 0;
    req->response_len = 0;

    fmrb_semaphore_give(ctx->sync_mutex);

    // Determine link_type from msg_type (same logic as async send)
    uint8_t link_type;
    if (msg_type == FMRB_CONTROL_CMD_INIT_DISPLAY) {
        link_type = FMRB_LINK_TYPE_CONTROL;
    } else {
        link_type = FMRB_LINK_TYPE_GRAPHICS;
    }

    // Send message
    fmrb_err_t ret = send_raw_message(link_type, seq, msg_type, payload, payload_len);
    if (ret != FMRB_OK) {
        // Mark slot as inactive on send failure
        fmrb_semaphore_take(ctx->sync_mutex, FMRB_TICK_MAX);
        req->active = false;
        fmrb_semaphore_give(ctx->sync_mutex);
        return ret;
    }

    // Wait for response
    fmrb_tick_t ticks = (timeout_ms == UINT32_MAX) ? FMRB_TICK_MAX : FMRB_MS_TO_TICKS(timeout_ms);
    fmrb_base_type_t wait_result = fmrb_semaphore_take(req->wait_sem, ticks);

    fmrb_semaphore_take(ctx->sync_mutex, FMRB_TICK_MAX);

    if (wait_result != FMRB_TRUE || !req->response_received) {
        // Timeout
        req->active = false;
        fmrb_semaphore_give(ctx->sync_mutex);
        FMRB_LOGW(TAG, "Sync send timeout for seq=%u", sequence);
        return FMRB_ERR_TIMEOUT;
    }

    // Response received
    uint8_t status = req->response_status;
    if (response_len) {
        *response_len = req->response_len;
    }
    req->active = false;

    fmrb_semaphore_give(ctx->sync_mutex);

    if (status != 0) {
        FMRB_LOGW(TAG, "Sync send received error response: status=%u", status);
        return FMRB_ERR_FAILED;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_link_transport_register_callback(uint8_t msg_type,
                                                 fmrb_link_transport_callback_t callback,
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

fmrb_err_t fmrb_link_transport_unregister_callback(uint8_t msg_type) {
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
    if (sub_cmd == FMRB_LINK_MSG_ACK || sub_cmd == FMRB_LINK_MSG_NACK) {
        uint8_t response_status = (sub_cmd == FMRB_LINK_MSG_ACK) ? 0 : 1;
        uint16_t original_sequence = seq;
        const uint8_t *response_data = payload;
        uint32_t response_data_len = payload_len;

        // Check if this is a response to a sync request
        fmrb_semaphore_take(ctx->sync_mutex, FMRB_TICK_MAX);
        for (int i = 0; i < MAX_SYNC_REQUESTS; i++) {
            if (ctx->sync_requests[i].active && ctx->sync_requests[i].sequence == original_sequence) {
                sync_request_t *req = &ctx->sync_requests[i];
                req->response_received = true;
                req->response_status = response_status;

                // Copy response data if provided
                if (response_data && response_data_len > 0 && req->response_payload) {
                    uint32_t copy_len = (response_data_len < req->response_max_len) ? response_data_len : req->response_max_len;
                    memcpy(req->response_payload, response_data, copy_len);
                    req->response_len = copy_len;
                }

                // Signal waiting thread
                fmrb_semaphore_give(req->wait_sem);
                fmrb_semaphore_give(ctx->sync_mutex);

                // Also remove from pending list for retransmit tracking
                for (int j = 0; j < ctx->pending_count; j++) {
                    if (ctx->pending_messages[j].sequence == original_sequence) {
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

        // Not a sync request, just remove from pending list
        for (int i = 0; i < ctx->pending_count; i++) {
            if (ctx->pending_messages[i].sequence == original_sequence) {
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
    send_raw_message(FMRB_LINK_TYPE_CONTROL, ack_seq, FMRB_LINK_MSG_ACK, (uint8_t*)&ack, sizeof(ack));
}

fmrb_err_t fmrb_link_transport_process(void) {
    transport_context_t *ctx = &g_tranport_context;
    if (!ctx->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Check for incoming messages
    fmrb_link_message_t hal_msg;
    if (fmrb_hal_link_receive(FMRB_LINK_GRAPHICS, &hal_msg, 0) == FMRB_OK) {
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

            handle_received_message(ctx, type, seq, sub_cmd, payload, payload_len);
        }

        msgpack_unpacked_destroy(&msg);
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

                    // Determine link_type from msg_type (same logic as in send())
                    uint8_t link_type;
                    if (pending->msg_type == FMRB_CONTROL_CMD_INIT_DISPLAY) {
                        link_type = FMRB_LINK_TYPE_CONTROL;
                    } else {
                        link_type = FMRB_LINK_TYPE_GRAPHICS;
                    }

                    send_raw_message(link_type, seq, pending->msg_type, pending->payload, pending->payload_len);
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

    return FMRB_OK;
}

fmrb_link_transport_handle_t fmrb_link_transport_get_handle(void) {
    return g_tranport_context.initialized ? &g_tranport_context : NULL;
}