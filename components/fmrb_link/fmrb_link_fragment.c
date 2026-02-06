#include "fmrb_link_fragment.h"
#include "fmrb_mem.h"
#include <string.h>

// Initialize fragment manager
void fmrb_fragment_manager_init(fmrb_fragment_manager_t *manager) {
    if (!manager) return;

    memset(manager, 0, sizeof(fmrb_fragment_manager_t));

    for (int i = 0; i < FMRB_LINK_FRAG_MAX_CONCURRENT; i++) {
        manager->contexts[i].state = FMRB_FRAG_STATE_IDLE;
        manager->contexts[i].buffer = NULL;
    }

    manager->next_chunk_id = 1; // Start from 1 (0 reserved for non-chunked)
}

// Cleanup fragment manager
void fmrb_fragment_manager_cleanup(fmrb_fragment_manager_t *manager) {
    if (!manager) return;

    for (int i = 0; i < FMRB_LINK_FRAG_MAX_CONCURRENT; i++) {
        fmrb_fragment_free_context(&manager->contexts[i]);
    }
}

// Check if message needs fragmentation
bool fmrb_fragment_needs_chunking(size_t payload_len) {
    return payload_len > FMRB_LINK_FRAG_CHUNK_THRESHOLD;
}

// Calculate number of chunks needed
uint32_t fmrb_fragment_calculate_num_chunks(uint32_t payload_len) {
    return (payload_len + FMRB_LINK_FRAG_MAX_CHUNK_PAYLOAD - 1) /
           FMRB_LINK_FRAG_MAX_CHUNK_PAYLOAD;
}

// Initialize send context
void fmrb_fragment_init_send_ctx(fmrb_fragment_send_ctx_t *ctx,
                                  const uint8_t *data,
                                  uint32_t len,
                                  uint8_t type,
                                  uint8_t seq,
                                  uint8_t chunk_id) {
    if (!ctx) return;

    ctx->data = data;
    ctx->total_len = len;
    ctx->offset = 0;
    ctx->chunk_id = chunk_id;
    ctx->type = type;
    ctx->seq = seq;
    ctx->window_used = 0;
}

// Get next chunk from send context
fmrb_err_t fmrb_fragment_get_next_chunk(fmrb_fragment_send_ctx_t *ctx,
                                        fmrb_link_chunk_info_t *chunk_info,
                                        const uint8_t **chunk_data,
                                        uint32_t *chunk_len) {
    if (!ctx || !chunk_info || !chunk_data || !chunk_len) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Check if all chunks sent
    if (ctx->offset >= ctx->total_len) {
        return FMRB_ERR_END;
    }

    // Calculate chunk length
    uint32_t remaining = ctx->total_len - ctx->offset;
    uint32_t this_chunk_len = (remaining > FMRB_LINK_FRAG_MAX_CHUNK_PAYLOAD) ?
                              FMRB_LINK_FRAG_MAX_CHUNK_PAYLOAD : remaining;

    // Fill chunk info
    chunk_info->chunk_id = ctx->chunk_id;
    chunk_info->chunk_len = this_chunk_len;
    chunk_info->offset = ctx->offset;
    chunk_info->total_len = ctx->total_len;
    chunk_info->flags = 0;

    // Set START flag for first chunk
    if (ctx->offset == 0) {
        chunk_info->flags |= FMRB_LINK_CHUNK_FL_START;
    }

    // Set END flag for last chunk
    if (ctx->offset + this_chunk_len >= ctx->total_len) {
        chunk_info->flags |= FMRB_LINK_CHUNK_FL_END;
    }

    // Return chunk data pointer and length
    *chunk_data = ctx->data + ctx->offset;
    *chunk_len = this_chunk_len;

    // Advance offset
    ctx->offset += this_chunk_len;
    ctx->window_used++;

    return FMRB_OK;
}

// Find or allocate reassembly context
fmrb_fragment_reassembly_ctx_t* fmrb_fragment_find_context(
    fmrb_fragment_manager_t *manager,
    uint8_t chunk_id,
    bool create) {

    if (!manager) return NULL;

    // First, try to find existing context
    for (int i = 0; i < FMRB_LINK_FRAG_MAX_CONCURRENT; i++) {
        if (manager->contexts[i].state != FMRB_FRAG_STATE_IDLE &&
            manager->contexts[i].chunk_id == chunk_id) {
            return &manager->contexts[i];
        }
    }

    // If not found and create requested, allocate new
    if (create) {
        for (int i = 0; i < FMRB_LINK_FRAG_MAX_CONCURRENT; i++) {
            if (manager->contexts[i].state == FMRB_FRAG_STATE_IDLE) {
                memset(&manager->contexts[i], 0, sizeof(fmrb_fragment_reassembly_ctx_t));
                manager->contexts[i].chunk_id = chunk_id;
                manager->contexts[i].state = FMRB_FRAG_STATE_RECEIVING;
                return &manager->contexts[i];
            }
        }
    }

    return NULL; // No available context
}

// Process received chunk
fmrb_err_t fmrb_fragment_process_chunk(fmrb_fragment_reassembly_ctx_t *ctx,
                                       const fmrb_link_chunk_info_t *chunk_info,
                                       const uint8_t *chunk_data,
                                       uint32_t chunk_len,
                                       uint32_t current_time_ms) {
    if (!ctx || !chunk_info || !chunk_data) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Validate chunk length
    if (chunk_len != chunk_info->chunk_len) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Check for error flag
    if (chunk_info->flags & FMRB_LINK_CHUNK_FL_ERR) {
        ctx->state = FMRB_FRAG_STATE_ERROR;
        return FMRB_ERR_FAILED;
    }

    // Handle START chunk
    if (chunk_info->flags & FMRB_LINK_CHUNK_FL_START) {
        // Free existing buffer if any
        if (ctx->buffer) {
            fmrb_sys_free(ctx->buffer);
        }

        // Allocate new buffer for total length
        ctx->buffer = (uint8_t*)fmrb_sys_malloc(chunk_info->total_len);
        if (!ctx->buffer) {
            ctx->state = FMRB_FRAG_STATE_ERROR;
            return FMRB_ERR_NO_MEMORY;
        }

        ctx->total_len = chunk_info->total_len;
        ctx->received_bytes = 0;
        ctx->last_offset = 0;
    }

    // Validate buffer exists
    if (!ctx->buffer) {
        ctx->state = FMRB_FRAG_STATE_ERROR;
        return FMRB_ERR_INVALID_STATE;
    }

    // Validate offset
    if (chunk_info->offset + chunk_len > ctx->total_len) {
        ctx->state = FMRB_FRAG_STATE_ERROR;
        return FMRB_ERR_INVALID_PARAM;
    }

    // Copy chunk data to buffer
    memcpy(ctx->buffer + chunk_info->offset, chunk_data, chunk_len);
    ctx->received_bytes += chunk_len;
    ctx->last_offset = chunk_info->offset + chunk_len;
    ctx->last_update_time_ms = current_time_ms;

    // Check if complete
    if (chunk_info->flags & FMRB_LINK_CHUNK_FL_END) {
        if (ctx->received_bytes == ctx->total_len) {
            ctx->state = FMRB_FRAG_STATE_COMPLETE;
            return FMRB_ERR_COMPLETE; // Special return code for completion
        } else {
            // END flag but incomplete data
            ctx->state = FMRB_FRAG_STATE_ERROR;
            return FMRB_ERR_FAILED;
        }
    }

    return FMRB_OK;
}

// Check for expired contexts
int fmrb_fragment_cleanup_expired(fmrb_fragment_manager_t *manager,
                                  uint32_t current_time_ms) {
    if (!manager) return 0;

    int cleaned = 0;

    for (int i = 0; i < FMRB_LINK_FRAG_MAX_CONCURRENT; i++) {
        fmrb_fragment_reassembly_ctx_t *ctx = &manager->contexts[i];

        if (ctx->state == FMRB_FRAG_STATE_RECEIVING) {
            uint32_t elapsed = current_time_ms - ctx->last_update_time_ms;

            if (elapsed > FMRB_LINK_FRAG_TIMEOUT_MS) {
                // Timeout - cleanup
                fmrb_fragment_free_context(ctx);
                cleaned++;
            }
        }
    }

    return cleaned;
}

// Free reassembly context
void fmrb_fragment_free_context(fmrb_fragment_reassembly_ctx_t *ctx) {
    if (!ctx) return;

    if (ctx->buffer) {
        fmrb_sys_free(ctx->buffer);
        ctx->buffer = NULL;
    }

    ctx->state = FMRB_FRAG_STATE_IDLE;
    ctx->total_len = 0;
    ctx->received_bytes = 0;
}

// Generate chunk ACK
void fmrb_fragment_generate_ack(const fmrb_fragment_reassembly_ctx_t *ctx,
                                fmrb_link_frame_chunk_ack_t *ack,
                                uint8_t gen) {
    if (!ctx || !ack) return;

    ack->chunk_id = ctx->chunk_id;
    ack->gen = gen;
    ack->credit = FMRB_LINK_FRAG_WINDOW_SIZE; // Allow full window
    ack->next_offset = ctx->last_offset;
}

// Allocate next chunk ID
uint8_t fmrb_fragment_alloc_chunk_id(fmrb_fragment_manager_t *manager) {
    if (!manager) return 0;

    uint8_t id = manager->next_chunk_id;
    manager->next_chunk_id++;

    // Wrap around, skip 0
    if (manager->next_chunk_id == 0) {
        manager->next_chunk_id = 1;
    }

    return id;
}
