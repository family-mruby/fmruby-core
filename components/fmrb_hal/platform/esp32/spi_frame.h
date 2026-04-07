#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "fmrb_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_FRAME_MAGIC  0xA5

typedef enum {
    STS_BOOT    = 0x00,  // Initial state (after boot)
    STS_RX_OK   = 0x10,  // Frame received (app not yet processed)
    STS_APP_OK  = 0x12,  // App processing succeeded
    STS_APP_ERR = 0x13,  // App processing failed
    STS_CRC_ERR = 0xE1,  // Frame CRC error
} spi_status_t;

typedef struct __attribute__((packed)) {
    uint8_t magic;          // 0xA5: frame validity check
    uint8_t seq;            // Master: command seq / Slave: 0
    uint8_t ack_seq;        // Slave: ack target master seq / Master: 0
    uint8_t status;         // Slave: STS_* / Master: 0
    uint16_t data_len;      // COBS payload length (0 = no data, includes 0x00 delimiters)
    uint8_t data[FMRB_LINK_FRAME_MAX_DATA];  // COBS encoded messages (msgpack)
    uint16_t crc16;         // CRC16-CCITT over first (FMRB_LINK_FRAME_SIZE - 2) bytes
} spi_frame_t;              // FMRB_LINK_FRAME_SIZE bytes total

// CRC16-CCITT (polynomial 0x1021, init 0xFFFF)
static inline uint16_t crc16_ccitt(const uint8_t *data, size_t len)
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

// Build frame header + CRC16 (data[] must be filled before calling)
static inline void spi_frame_finalize(spi_frame_t *f)
{
    f->crc16 = crc16_ccitt((const uint8_t *)f, FMRB_LINK_FRAME_SIZE - FMRB_LINK_FRAME_CRC_SIZE);
}

// Validate frame (magic + CRC16)
static inline bool spi_frame_validate(const spi_frame_t *f)
{
    if (f->magic != SPI_FRAME_MAGIC) return false;
    uint16_t expected = crc16_ccitt((const uint8_t *)f, FMRB_LINK_FRAME_SIZE - FMRB_LINK_FRAME_CRC_SIZE);
    return expected == f->crc16;
}

#ifdef __cplusplus
}
#endif
