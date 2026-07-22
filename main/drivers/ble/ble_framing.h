/**
 * @file ble_framing.h
 * @brief Frame codec shared by the BLE services (file service, debug service).
 *
 * Both services send `COBS(payload + CRC32(payload)) + 0x00`. COBS guarantees
 * the encoded bytes never contain 0x00, so the delimiter unambiguously marks
 * the end of a frame in the BLE write stream. The CRC32 is computed over the
 * plain payload, before encoding.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/** Frame delimiter appended after every COBS-encoded frame. */
#define BLE_FRAMING_DELIM 0x00

/**
 * @brief CRC32 (polynomial 0xEDB88320, the zlib/PNG variant).
 * @param data Bytes to checksum.
 * @param len  Number of bytes.
 * @return CRC32 of the input.
 */
uint32_t ble_framing_crc32(const uint8_t *data, size_t len);

/**
 * @brief COBS-encode a buffer (the trailing delimiter is NOT appended).
 * @param input       Plain bytes to encode.
 * @param input_len   Number of plain bytes.
 * @param output      Destination buffer.
 * @param output_size Capacity of @p output.
 * @return Encoded byte count, or 0 if @p output is too small.
 */
size_t ble_framing_cobs_encode(const uint8_t *input, size_t input_len,
                               uint8_t *output, size_t output_size);

/**
 * @brief COBS-decode a buffer. Stops at a 0x00 byte or at @p input_len.
 * @param input       Encoded bytes (delimiter may be present or omitted).
 * @param input_len   Number of encoded bytes.
 * @param output      Destination buffer.
 * @param output_size Capacity of @p output; decoding stops when full.
 * @return Decoded byte count.
 */
size_t ble_framing_cobs_decode(const uint8_t *input, size_t input_len,
                               uint8_t *output, size_t output_size);
