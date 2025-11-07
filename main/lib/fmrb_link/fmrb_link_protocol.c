#include "fmrb_link_protocol.h"
#include "fmrb_link_cobs.h"
#include "fmrb_mem.h"
#include <string.h>
#include <stdlib.h>

// Legacy CRC32 table and functions removed - fmrb_link_header_t is no longer used
// Use fmrb_link_frame_encode/decode for IPC_spec.md compliant framing with built-in CRC32

// Frame encode/decode (IPC_spec.md compliant)
ssize_t fmrb_link_frame_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t payload_len,
                               uint8_t *output, size_t output_len) {
    if (!output || output_len == 0) {
        return -1;
    }

    // Calculate required size: header + payload + CRC32
    size_t frame_size = sizeof(fmrb_link_frame_hdr_t) + payload_len + sizeof(uint32_t);
    size_t max_encoded_size = COBS_ENC_MAX(frame_size);

    if (output_len < max_encoded_size) {
        return -1; // Output buffer too small
    }

    // Build frame: [header | payload | CRC32]
    uint8_t *temp_buffer = (uint8_t*)fmrb_sys_malloc(frame_size);
    if (!temp_buffer) {
        return -1;
    }

    // Fill header
    fmrb_link_frame_hdr_t *hdr = (fmrb_link_frame_hdr_t*)temp_buffer;
    hdr->type = type;
    hdr->seq = seq;
    hdr->len = payload_len;

    // Copy payload
    if (payload && payload_len > 0) {
        memcpy(temp_buffer + sizeof(fmrb_link_frame_hdr_t), payload, payload_len);
    }

    // Calculate CRC32 of header + payload
    uint32_t crc = fmrb_link_crc32_update(0, temp_buffer, sizeof(fmrb_link_frame_hdr_t) + payload_len);
    memcpy(temp_buffer + sizeof(fmrb_link_frame_hdr_t) + payload_len, &crc, sizeof(uint32_t));

    // COBS encode
    size_t encoded_len = fmrb_link_cobs_encode(temp_buffer, frame_size, output);

    fmrb_sys_free(temp_buffer);
    return (ssize_t)encoded_len;
}

int32_t fmrb_link_frame_decode(const uint8_t *input, size_t input_len,
                               fmrb_link_frame_hdr_t *hdr, uint8_t *payload,
                               size_t payload_max_len, uint16_t *payload_len) {
    if (!input || !hdr || !payload || !payload_len) {
        return -1;
    }

    // Allocate temporary buffer for decoded data
    uint8_t *temp_buffer = (uint8_t*)fmrb_sys_malloc(input_len);
    if (!temp_buffer) {
        return -1;
    }

    // COBS decode
    ssize_t decoded_len = fmrb_link_cobs_decode(input, input_len, temp_buffer);
    if (decoded_len < (ssize_t)(sizeof(fmrb_link_frame_hdr_t) + sizeof(uint32_t))) {
        fmrb_sys_free(temp_buffer);
        return -1; // Too small
    }

    // Extract header
    memcpy(hdr, temp_buffer, sizeof(fmrb_link_frame_hdr_t));

    // Verify payload length
    if (hdr->len > payload_max_len) {
        fmrb_sys_free(temp_buffer);
        return -1; // Payload buffer too small
    }

    // Extract CRC32
    size_t expected_size = sizeof(fmrb_link_frame_hdr_t) + hdr->len + sizeof(uint32_t);
    if ((size_t)decoded_len != expected_size) {
        fmrb_sys_free(temp_buffer);
        return -1; // Size mismatch
    }

    uint32_t received_crc;
    memcpy(&received_crc, temp_buffer + sizeof(fmrb_link_frame_hdr_t) + hdr->len, sizeof(uint32_t));

    // Verify CRC32
    uint32_t calculated_crc = fmrb_link_crc32_update(0, temp_buffer, sizeof(fmrb_link_frame_hdr_t) + hdr->len);
    if (received_crc != calculated_crc) {
        fmrb_sys_free(temp_buffer);
        return -1; // CRC mismatch
    }

    // Copy payload
    if (hdr->len > 0) {
        memcpy(payload, temp_buffer + sizeof(fmrb_link_frame_hdr_t), hdr->len);
    }
    *payload_len = hdr->len;

    fmrb_sys_free(temp_buffer);
    return 0;
}