#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * COBS (Consistent Overhead Byte Stuffing) encoding/decoding
 * Used for framing data with 0x00 as delimiter
 */

#define COBS_FRAME_TERM 0x00

/**
 * @brief Calculate maximum encoded size for given input size
 * @param input_len Input data length
 * @return Maximum encoded size (includes overhead bytes + terminator)
 */
#define COBS_ENC_MAX(input_len) ((input_len) + ((input_len) / 254) + 2)

/**
 * @brief Encode data using COBS
 * @param input Input data
 * @param input_len Input data length
 * @param output Output buffer (must be at least COBS_ENC_MAX(input_len) bytes)
 * @return Encoded data length (including 0x00 terminator)
 */
size_t fmrb_link_cobs_encode(const uint8_t *input, size_t input_len, uint8_t *output);

/**
 * @brief Decode COBS encoded data
 * @param input Encoded data (without 0x00 terminator)
 * @param input_len Encoded data length
 * @param output Output buffer (must be at least input_len bytes)
 * @return Decoded data length, or -1 on error
 */
ssize_t fmrb_link_cobs_decode(const uint8_t *input, size_t input_len, uint8_t *output);

/**
 * @brief Calculate CRC32 checksum
 * @param crc Initial CRC value (use 0 for new calculation)
 * @param data Data to checksum
 * @param len Data length
 * @return CRC32 checksum
 */
uint32_t fmrb_link_crc32_update(uint32_t crc, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
