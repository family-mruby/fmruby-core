#include "fmrb_ipc_transport.h"
#include "fmrb_ipc_protocol.h"
#include "fmrb_hal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <msgpack.h>

#define MAX_CALLBACKS 16
#define MAX_PENDING_MESSAGES 8

typedef struct {
    uint8_t msg_type;
    fmrb_ipc_transport_callback_t callback;
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

typedef struct {
    fmrb_ipc_transport_config_t config;
    uint16_t next_sequence;

    callback_entry_t callbacks[MAX_CALLBACKS];
    int callback_count;

    pending_message_t pending_messages[MAX_PENDING_MESSAGES];
    int pending_count;

    bool initialized;
} transport_context_t;

static const char *TAG = "fmrb_ipc_transport";

fmrb_ipc_transport_err_t fmrb_ipc_transport_init(const fmrb_ipc_transport_config_t *config,
                                                  fmrb_ipc_transport_handle_t *handle) {
    if (!config || !handle) {
        return FMRB_IPC_TRANSPORT_ERR_INVALID_PARAM;
    }

    transport_context_t *ctx = malloc(sizeof(transport_context_t));
    if (!ctx) {
        return FMRB_IPC_TRANSPORT_ERR_NO_MEMORY;
    }

    memset(ctx, 0, sizeof(transport_context_t));
    ctx->config = *config;
    ctx->next_sequence = 1;
    ctx->initialized = true;

    *handle = ctx;
    return FMRB_IPC_TRANSPORT_OK;
}

fmrb_ipc_transport_err_t fmrb_ipc_transport_deinit(fmrb_ipc_transport_handle_t handle) {
    if (!handle) {
        return FMRB_IPC_TRANSPORT_ERR_INVALID_PARAM;
    }

    transport_context_t *ctx = (transport_context_t *)handle;

    // Free pending messages
    for (int i = 0; i < ctx->pending_count; i++) {
        if (ctx->pending_messages[i].payload) {
            free(ctx->pending_messages[i].payload);
        }
    }

    ctx->initialized = false;
    free(ctx);
    return FMRB_IPC_TRANSPORT_OK;
}

static fmrb_ipc_transport_err_t send_raw_message(const fmrb_ipc_header_t *header, const uint8_t *payload) {
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
    msgpack_pack_uint8(&pk, FMRB_IPC_TYPE_GRAPHICS);  // type
    msgpack_pack_uint8(&pk, (uint8_t)(header->sequence & 0xFF));  // seq
    msgpack_pack_uint8(&pk, header->msg_type);  // sub_cmd (graphics command)

    // Pack payload as binary
    if (payload && header->payload_len > 0) {
        msgpack_pack_bin(&pk, header->payload_len);
        msgpack_pack_bin_body(&pk, payload, header->payload_len);
    } else {
        msgpack_pack_nil(&pk);
    }

    // Debug: dump msgpack bytes
    // printf("TX msgpack: type=%d seq=%d sub_cmd=0x%02x payload_len=%" PRIu32 " msgpack_size=%zu\n",
    //        FMRB_IPC_TYPE_GRAPHICS, (uint8_t)(header->sequence & 0xFF),
    //        header->msg_type, header->payload_len, sbuf.size);
    // printf("TX msgpack bytes (%zu): ", sbuf.size);
    // for (size_t i = 0; i < sbuf.size && i < 64; i++) {
    //     printf("%02X ", (uint8_t)sbuf.data[i]);
    //     if ((i + 1) % 16 == 0) printf("\n");
    // }
    // printf("\n");
    // fflush(stdout);

    // Send via HAL (HAL will add CRC32 and COBS encode)
    fmrb_ipc_message_t hal_msg = {
        .data = (uint8_t*)sbuf.data,
        .size = sbuf.size
    };

    fmrb_err_t ret = fmrb_hal_ipc_send(FMRB_IPC_GRAPHICS, &hal_msg, 1000);

    msgpack_sbuffer_destroy(&sbuf);

    return (ret == FMRB_OK) ? FMRB_IPC_TRANSPORT_OK : FMRB_IPC_TRANSPORT_ERR_FAILED;
}

static fmrb_ipc_transport_err_t add_pending_message(transport_context_t *ctx, uint16_t sequence,
                                                    uint8_t msg_type, const uint8_t *payload, uint32_t payload_len) {
    if (ctx->pending_count >= MAX_PENDING_MESSAGES) {
        return FMRB_IPC_TRANSPORT_ERR_FAILED;
    }

    pending_message_t *pending = &ctx->pending_messages[ctx->pending_count];
    pending->sequence = sequence;
    pending->msg_type = msg_type;
    pending->payload_len = payload_len;
    pending->sent_time = fmrb_hal_time_get_us();
    pending->retry_count = 0;

    if (payload_len > 0 && payload) {
        pending->payload = malloc(payload_len);
        if (!pending->payload) {
            return FMRB_IPC_TRANSPORT_ERR_NO_MEMORY;
        }
        memcpy(pending->payload, payload, payload_len);
    } else {
        pending->payload = NULL;
    }

    ctx->pending_count++;
    return FMRB_IPC_TRANSPORT_OK;
}

fmrb_ipc_transport_err_t fmrb_ipc_transport_send(fmrb_ipc_transport_handle_t handle,
                                                  uint8_t msg_type,
                                                  const uint8_t *payload,
                                                  uint32_t payload_len) {
    if (!handle) {
        return FMRB_IPC_TRANSPORT_ERR_INVALID_PARAM;
    }

    transport_context_t *ctx = (transport_context_t *)handle;
    if (!ctx->initialized) {
        return FMRB_IPC_TRANSPORT_ERR_FAILED;
    }

    // Create header
    fmrb_ipc_header_t header;
    uint16_t sequence = ctx->next_sequence++;
    fmrb_ipc_init_header(&header, msg_type, sequence, payload_len);

    // Calculate checksum if payload exists
    if (payload && payload_len > 0) {
        header.checksum = fmrb_ipc_calculate_checksum(payload, payload_len);
    }

    // Send message
    fmrb_ipc_transport_err_t ret = send_raw_message(&header, payload);
    if (ret != FMRB_IPC_TRANSPORT_OK) {
        return ret;
    }

    // Add to pending list if retransmit is enabled
    if (ctx->config.enable_retransmit) {
        add_pending_message(ctx, sequence, msg_type, payload, payload_len);
    }

    return FMRB_IPC_TRANSPORT_OK;
}

fmrb_ipc_transport_err_t fmrb_ipc_transport_send_sync(fmrb_ipc_transport_handle_t handle,
                                                       uint8_t msg_type,
                                                       const uint8_t *payload,
                                                       uint32_t payload_len,
                                                       uint8_t *response_payload,
                                                       uint32_t *response_len,
                                                       uint32_t timeout_ms) {
    // For now, just send asynchronously
    // TODO: Implement synchronous sending with response waiting
    return fmrb_ipc_transport_send(handle, msg_type, payload, payload_len);
}

fmrb_ipc_transport_err_t fmrb_ipc_transport_register_callback(fmrb_ipc_transport_handle_t handle,
                                                               uint8_t msg_type,
                                                               fmrb_ipc_transport_callback_t callback,
                                                               void *user_data) {
    if (!handle || !callback) {
        return FMRB_IPC_TRANSPORT_ERR_INVALID_PARAM;
    }

    transport_context_t *ctx = (transport_context_t *)handle;
    if (!ctx->initialized || ctx->callback_count >= MAX_CALLBACKS) {
        return FMRB_IPC_TRANSPORT_ERR_FAILED;
    }

    callback_entry_t *entry = &ctx->callbacks[ctx->callback_count];
    entry->msg_type = msg_type;
    entry->callback = callback;
    entry->user_data = user_data;

    ctx->callback_count++;
    return FMRB_IPC_TRANSPORT_OK;
}

fmrb_ipc_transport_err_t fmrb_ipc_transport_unregister_callback(fmrb_ipc_transport_handle_t handle,
                                                                 uint8_t msg_type) {
    if (!handle) {
        return FMRB_IPC_TRANSPORT_ERR_INVALID_PARAM;
    }

    transport_context_t *ctx = (transport_context_t *)handle;
    if (!ctx->initialized) {
        return FMRB_IPC_TRANSPORT_ERR_FAILED;
    }

    // Find and remove callback
    for (int i = 0; i < ctx->callback_count; i++) {
        if (ctx->callbacks[i].msg_type == msg_type) {
            // Shift remaining callbacks
            for (int j = i; j < ctx->callback_count - 1; j++) {
                ctx->callbacks[j] = ctx->callbacks[j + 1];
            }
            ctx->callback_count--;
            return FMRB_IPC_TRANSPORT_OK;
        }
    }

    return FMRB_IPC_TRANSPORT_ERR_FAILED;
}

static void handle_received_message(transport_context_t *ctx, const fmrb_ipc_header_t *header, const uint8_t *payload) {
    // Handle ACK/NACK messages
    if (header->msg_type == FMRB_IPC_MSG_ACK || header->msg_type == FMRB_IPC_MSG_NACK) {
        // Remove from pending list
        for (int i = 0; i < ctx->pending_count; i++) {
            if (ctx->pending_messages[i].sequence == header->sequence) {
                if (ctx->pending_messages[i].payload) {
                    free(ctx->pending_messages[i].payload);
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
        if (ctx->callbacks[i].msg_type == header->msg_type) {
            ctx->callbacks[i].callback(header, payload, ctx->callbacks[i].user_data);
            break;
        }
    }

    // Send ACK
    fmrb_ipc_ack_t ack = {
        .original_sequence = header->sequence,
        .status = 0
    };

    fmrb_ipc_header_t ack_header;
    fmrb_ipc_init_header(&ack_header, FMRB_IPC_MSG_ACK, ctx->next_sequence++, sizeof(ack));
    ack_header.checksum = fmrb_ipc_calculate_checksum((uint8_t*)&ack, sizeof(ack));

    send_raw_message(&ack_header, (uint8_t*)&ack);
}

fmrb_ipc_transport_err_t fmrb_ipc_transport_process(fmrb_ipc_transport_handle_t handle) {
    if (!handle) {
        return FMRB_IPC_TRANSPORT_ERR_INVALID_PARAM;
    }

    transport_context_t *ctx = (transport_context_t *)handle;
    if (!ctx->initialized) {
        return FMRB_IPC_TRANSPORT_ERR_FAILED;
    }

    // Check for incoming messages
    fmrb_ipc_message_t hal_msg;
    if (fmrb_hal_ipc_receive(FMRB_IPC_GRAPHICS, &hal_msg, 0) == FMRB_OK) {
        if (hal_msg.size >= sizeof(fmrb_ipc_header_t)) {
            fmrb_ipc_header_t *header = (fmrb_ipc_header_t *)hal_msg.data;

            if (fmrb_ipc_verify_header(header)) {
                uint8_t *payload = (hal_msg.size > sizeof(fmrb_ipc_header_t)) ?
                                   hal_msg.data + sizeof(fmrb_ipc_header_t) : NULL;

                // Verify checksum if payload exists
                if (payload && header->payload_len > 0) {
                    uint32_t calc_checksum = fmrb_ipc_calculate_checksum(payload, header->payload_len);
                    if (calc_checksum != header->checksum) {
                        return FMRB_IPC_TRANSPORT_ERR_CHECKSUM;
                    }
                }

                handle_received_message(ctx, header, payload);
            }
        }
    }

    // Handle retransmissions
    if (ctx->config.enable_retransmit) {
        fmrb_time_t current_time = fmrb_hal_time_get_us();

        for (int i = 0; i < ctx->pending_count; i++) {
            pending_message_t *pending = &ctx->pending_messages[i];

            if (fmrb_hal_time_is_timeout(pending->sent_time, ctx->config.timeout_ms * 1000)) {
                if (pending->retry_count < ctx->config.max_retries) {
                    // Retransmit
                    fmrb_ipc_header_t header;
                    fmrb_ipc_init_header(&header, pending->msg_type, pending->sequence, pending->payload_len);

                    if (pending->payload && pending->payload_len > 0) {
                        header.checksum = fmrb_ipc_calculate_checksum(pending->payload, pending->payload_len);
                    }

                    send_raw_message(&header, pending->payload);
                    pending->sent_time = current_time;
                    pending->retry_count++;
                } else {
                    // Max retries reached, remove from pending
                    if (pending->payload) {
                        free(pending->payload);
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

    return FMRB_IPC_TRANSPORT_OK;
}