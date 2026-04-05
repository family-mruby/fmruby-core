#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fmrb_link_protocol.h"
#include "fmrb_err.h"
#include "spi_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fragmentation configuration
// Chunked msgpack: [type(2) + seq(2) + sub_cmd(2) + bin_hdr(3) + chunk_info(12) + bin_hdr(3) + payload(N)]
// + COBS worst-case (~2 bytes). Total must fit within SPI_MAX_DATA.
#define FMRB_LINK_FRAG_CHUNKED_OVERHEAD 28
#define FMRB_LINK_FRAG_MAX_CHUNK_PAYLOAD (SPI_MAX_DATA - FMRB_LINK_FRAG_CHUNKED_OVERHEAD)
#define FMRB_LINK_FRAG_WINDOW_SIZE 8          // Sliding window size
#define FMRB_LINK_FRAG_MAX_CONCURRENT 4       // Max concurrent reassembly contexts
#define FMRB_LINK_FRAG_TIMEOUT_MS 5000        // Reassembly timeout

// Reassembly context state
typedef enum {
    FMRB_FRAG_STATE_IDLE = 0,
    FMRB_FRAG_STATE_RECEIVING,
    FMRB_FRAG_STATE_COMPLETE,
    FMRB_FRAG_STATE_ERROR
} fmrb_fragment_state_t;

// Reassembly context for receiving fragmented messages
typedef struct {
    uint8_t chunk_id;                 // Chunk identifier (0-255)
    fmrb_fragment_state_t state;      // Current state
    uint8_t *buffer;                  // Reassembly buffer (dynamically allocated)
    uint32_t total_len;               // Total expected length
    uint32_t received_bytes;          // Bytes received so far
    uint32_t last_offset;             // Last received offset
    uint32_t last_update_time_ms;     // Timestamp of last update
    uint8_t type;                     // Message type
    uint8_t seq;                      // Sequence number
} fmrb_fragment_reassembly_ctx_t;

// Fragmentation context for sending large messages
typedef struct {
    const uint8_t *data;              // Source data pointer
    uint32_t total_len;               // Total data length
    uint32_t offset;                  // Current offset
    uint8_t chunk_id;                 // Assigned chunk ID
    uint8_t type;                     // Message type
    uint8_t seq;                      // Sequence number
    uint16_t window_used;             // Current window usage
} fmrb_fragment_send_ctx_t;

// Fragment manager - manages multiple concurrent reassembly contexts
typedef struct {
    fmrb_fragment_reassembly_ctx_t contexts[FMRB_LINK_FRAG_MAX_CONCURRENT];
    uint8_t next_chunk_id;            // Next chunk ID to assign
} fmrb_fragment_manager_t;

/**
 * Initialize fragment manager
 * @param manager Fragment manager instance
 */
void fmrb_fragment_manager_init(fmrb_fragment_manager_t *manager);

/**
 * Cleanup fragment manager (free all buffers)
 * @param manager Fragment manager instance
 */
void fmrb_fragment_manager_cleanup(fmrb_fragment_manager_t *manager);

/**
 * Calculate number of chunks needed for a message
 * @param payload_len Payload length
 * @return Number of chunks needed
 */
uint32_t fmrb_fragment_calculate_num_chunks(uint32_t payload_len);

/**
 * Initialize send context for fragmentation
 * @param ctx Send context
 * @param data Source data
 * @param len Data length
 * @param type Message type
 * @param seq Sequence number
 * @param chunk_id Assigned chunk ID
 */
void fmrb_fragment_init_send_ctx(fmrb_fragment_send_ctx_t *ctx,
                                  const uint8_t *data,
                                  uint32_t len,
                                  uint8_t type,
                                  uint8_t seq,
                                  uint8_t chunk_id);

/**
 * Get next chunk from send context
 * @param ctx Send context
 * @param chunk_info Output chunk info header (check flags for FMRB_LINK_CHUNK_FL_END to detect last chunk)
 * @param chunk_data Output pointer to chunk data
 * @param chunk_len Output chunk data length
 * @return FMRB_OK on success, error code on failure
 * @note Check chunk_info->flags & FMRB_LINK_CHUNK_FL_END to determine if this is the last chunk
 */
fmrb_err_t fmrb_fragment_get_next_chunk(fmrb_fragment_send_ctx_t *ctx,
                                        fmrb_link_chunk_info_t *chunk_info,
                                        const uint8_t **chunk_data,
                                        uint32_t *chunk_len);

/**
 * Find or allocate reassembly context
 * @param manager Fragment manager
 * @param chunk_id Chunk identifier
 * @param create If true, create new context if not found
 * @return Context pointer or NULL
 */
fmrb_fragment_reassembly_ctx_t* fmrb_fragment_find_context(
    fmrb_fragment_manager_t *manager,
    uint8_t chunk_id,
    bool create);

/**
 * Process received chunk
 * @param ctx Reassembly context
 * @param chunk_info Chunk header
 * @param chunk_data Chunk payload
 * @param chunk_len Chunk payload length
 * @param current_time_ms Current timestamp
 * @return FMRB_OK on success, error code on failure
 * @note Check ctx->state for FMRB_FRAG_STATE_COMPLETE to detect message completion
 */
fmrb_err_t fmrb_fragment_process_chunk(fmrb_fragment_reassembly_ctx_t *ctx,
                                       const fmrb_link_chunk_info_t *chunk_info,
                                       const uint8_t *chunk_data,
                                       uint32_t chunk_len,
                                       uint32_t current_time_ms);

/**
 * Check for expired contexts and clean them up
 * @param manager Fragment manager
 * @param current_time_ms Current timestamp
 * @return Number of contexts cleaned up
 */
int fmrb_fragment_cleanup_expired(fmrb_fragment_manager_t *manager,
                                  uint32_t current_time_ms);

/**
 * Free reassembly context
 * @param ctx Context to free
 */
void fmrb_fragment_free_context(fmrb_fragment_reassembly_ctx_t *ctx);

/**
 * Generate chunk ACK
 * @param ctx Reassembly context
 * @param ack Output ACK structure
 * @param gen Generation counter
 */
void fmrb_fragment_generate_ack(const fmrb_fragment_reassembly_ctx_t *ctx,
                                fmrb_link_frame_chunk_ack_t *ack,
                                uint8_t gen);

/**
 * Allocate next chunk ID
 * @param manager Fragment manager
 * @return Chunk ID (0-255, wraps around)
 */
uint8_t fmrb_fragment_alloc_chunk_id(fmrb_fragment_manager_t *manager);

#ifdef __cplusplus
}
#endif
