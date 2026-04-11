#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// UART link wire format:
//   [SYNC 1B] [HEADER 6B] [DATA data_len B] [CRC16 2B]
//
// Total on wire: 1 + 6 + data_len + 2 = 9 + data_len bytes

#define UART_LINK_SYNC_BYTE     0x55
#define UART_LINK_MAGIC         0xA5
#define UART_LINK_HEADER_SIZE   6
#define UART_LINK_CRC_SIZE      2
#define UART_LINK_MAX_DATA      248   // Same as SPI initial value; can be increased later

// Status codes (shared with SPI, same values as spi_frame.h)
#define UART_LINK_STS_BOOT      0x00
#define UART_LINK_STS_RX_OK     0x10
#define UART_LINK_STS_APP_OK    0x12
#define UART_LINK_STS_APP_ERR   0x13
#define UART_LINK_STS_CRC_ERR   0xE1

typedef struct __attribute__((packed)) {
    uint8_t magic;          // UART_LINK_MAGIC (0xA5)
    uint8_t seq;            // Master: command sequence / Slave: 0
    uint8_t ack_seq;        // Slave: ack target seq / Master: 0
    uint8_t status;         // Slave: STS_* / Master: 0
    uint16_t data_len;      // Payload length in bytes (little-endian)
} uart_link_header_t;

_Static_assert(sizeof(uart_link_header_t) == UART_LINK_HEADER_SIZE,
               "uart_link_header_t size mismatch");

// Maximum frame size on wire (for buffer allocation)
#define UART_LINK_MAX_FRAME_SIZE  (1 + UART_LINK_HEADER_SIZE + UART_LINK_MAX_DATA + UART_LINK_CRC_SIZE)

// CRC16-CCITT (polynomial 0x1021, init 0xFFFF)
// Same algorithm as spi_frame.h
static inline uint16_t uart_link_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

// Build a frame into tx_buf. Returns total frame size (including sync + CRC).
// tx_buf must be at least UART_LINK_MAX_FRAME_SIZE bytes.
// data can be NULL if data_len == 0.
static inline size_t uart_link_build_frame(uint8_t *tx_buf,
                                           uint8_t seq, uint8_t ack_seq,
                                           uint8_t status,
                                           const uint8_t *data, uint16_t data_len)
{
    size_t pos = 0;

    // Sync byte
    tx_buf[pos++] = UART_LINK_SYNC_BYTE;

    // Header
    uart_link_header_t *hdr = (uart_link_header_t *)(tx_buf + pos);
    hdr->magic = UART_LINK_MAGIC;
    hdr->seq = seq;
    hdr->ack_seq = ack_seq;
    hdr->status = status;
    hdr->data_len = data_len;
    pos += UART_LINK_HEADER_SIZE;

    // Data
    if (data && data_len > 0) {
        memcpy(tx_buf + pos, data, data_len);
    }
    pos += data_len;

    // CRC16 over header + data
    uint16_t crc = uart_link_crc16(tx_buf + 1, UART_LINK_HEADER_SIZE + data_len);
    tx_buf[pos++] = (uint8_t)(crc & 0xFF);
    tx_buf[pos++] = (uint8_t)(crc >> 8);

    return pos;
}

// Validate a received frame (header + data + CRC already in buffer).
// buf points to the header (after sync byte), length = HEADER_SIZE + data_len + CRC_SIZE.
static inline bool uart_link_validate_frame(const uint8_t *buf, size_t buf_len)
{
    if (buf_len < UART_LINK_HEADER_SIZE + UART_LINK_CRC_SIZE) {
        return false;
    }

    const uart_link_header_t *hdr = (const uart_link_header_t *)buf;
    if (hdr->magic != UART_LINK_MAGIC) {
        return false;
    }
    if (hdr->data_len > UART_LINK_MAX_DATA) {
        return false;
    }

    size_t expected_len = UART_LINK_HEADER_SIZE + hdr->data_len + UART_LINK_CRC_SIZE;
    if (buf_len < expected_len) {
        return false;
    }

    // CRC16 is over header + data (excluding CRC bytes themselves)
    size_t crc_data_len = UART_LINK_HEADER_SIZE + hdr->data_len;
    uint16_t expected_crc = uart_link_crc16(buf, crc_data_len);
    uint16_t actual_crc = (uint16_t)buf[crc_data_len] | ((uint16_t)buf[crc_data_len + 1] << 8);

    return expected_crc == actual_crc;
}

#ifdef __cplusplus
}
#endif
