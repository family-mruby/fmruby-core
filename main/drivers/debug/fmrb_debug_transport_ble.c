// BLE transport for the remote debugger (ESP32 targets).
//
// Frame = COBS([u16 BE body length][msgpack body][CRC32 4B BE]) + 0x00, the
// same codec the BLE file service uses (ble_framing.h). The GATT service in
// ble_task.c accumulates raw bytes on the NimBLE host task and hands finished
// frames over queues; decoding and verification happen here, on the debugd
// task. poll() and send() are therefore called from that one task only and
// need no locking around their scratch buffers.
#include "fmrb_debug_transport.h"
#include "fmrb_debug_proto.h"

#include <string.h>

#include "esp_attr.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "ble_debug_link.h"
#include "ble_framing.h"

static const char *TAG = "dbg_ble";

// Plain frame: [u16 BE body length][body][CRC32 4B BE]
#define BLE_DBG_PLAIN_MAX (FMRB_DEBUG_MAX_FRAME + 2 + 4)

static fmrb_queue_t s_free_q;
static fmrb_queue_t s_ready_q;

// Scratch buffers. Both are touched by the debugd task only.
EXT_RAM_BSS_ATTR static uint8_t s_plain[BLE_DBG_PLAIN_MAX];
EXT_RAM_BSS_ATTR static uint8_t s_enc[BLE_DBG_MAX_ENC];

static fmrb_err_t ble_init(void) {
    s_free_q  = fmrb_queue_create(BLE_DBG_RX_BUF_COUNT, sizeof(ble_dbg_frame_ref_t));
    s_ready_q = fmrb_queue_create(BLE_DBG_RX_BUF_COUNT, sizeof(ble_dbg_frame_ref_t));
    if (!s_free_q || !s_ready_q) {
        FMRB_LOGE(TAG, "queue create failed");
        if (s_free_q)  { fmrb_queue_delete(s_free_q);  s_free_q = NULL; }
        if (s_ready_q) { fmrb_queue_delete(s_ready_q); s_ready_q = NULL; }
        return FMRB_ERR_NO_MEMORY;
    }

    for (uint8_t i = 0; i < BLE_DBG_RX_BUF_COUNT; i++) {
        ble_dbg_frame_ref_t ref = { .idx = i, .len = 0 };
        fmrb_queue_send(s_free_q, &ref, 0);
    }

    // Registration only stores the handles; it does not touch the BLE stack.
    // That keeps debugd independent of BLE bring-up, which on the P4 happens
    // on a deferred task after the C6 radio link is up.
    fmrb_err_t err = ble_debug_register(s_free_q, s_ready_q);
    if (err != FMRB_OK) {
        FMRB_LOGE(TAG, "ble_debug_register failed: %d", (int)err);
        return err;
    }

    FMRB_LOGI(TAG, "BLE debug transport ready");
    return FMRB_OK;
}

// Decode one received frame into buf. Returns body length, or 0 if the frame
// is malformed (dropped with a warning).
static int decode_frame(const uint8_t *enc, uint16_t enc_len, uint8_t *buf, size_t cap) {
    size_t plain_len = ble_framing_cobs_decode(enc, enc_len, s_plain, sizeof(s_plain));
    if (plain_len < 6) {
        FMRB_LOGW(TAG, "frame too short (%zu bytes after COBS)", plain_len);
        return 0;
    }

    uint32_t got_crc = ((uint32_t)s_plain[plain_len - 4] << 24) |
                       ((uint32_t)s_plain[plain_len - 3] << 16) |
                       ((uint32_t)s_plain[plain_len - 2] << 8)  |
                       ((uint32_t)s_plain[plain_len - 1]);
    uint32_t calc_crc = ble_framing_crc32(s_plain, plain_len - 4);
    if (got_crc != calc_crc) {
        FMRB_LOGW(TAG, "CRC mismatch: got=0x%08lx calc=0x%08lx",
                  (unsigned long)got_crc, (unsigned long)calc_crc);
        return 0;
    }

    size_t body_len = ((size_t)s_plain[0] << 8) | s_plain[1];
    if (body_len != plain_len - 6) {
        FMRB_LOGW(TAG, "length mismatch: header=%zu actual=%zu",
                  body_len, plain_len - 6);
        return 0;
    }
    if (body_len > cap) {
        FMRB_LOGW(TAG, "body %zu > cap %zu", body_len, cap);
        return 0;
    }

    memcpy(buf, s_plain + 2, body_len);
    return (int)body_len;
}

static int ble_poll(uint8_t *buf, size_t cap, uint32_t timeout_ms) {
    if (!s_ready_q) {
        // Unreachable once init() succeeded; still honour the timeout rather
        // than spinning the debugd task at full speed.
        fmrb_task_delay(FMRB_MS_TO_TICKS(timeout_ms));
        return 0;
    }

    ble_dbg_frame_ref_t ref;
    if (fmrb_queue_receive(s_ready_q, &ref, FMRB_MS_TO_TICKS(timeout_ms)) != FMRB_TRUE) {
        return 0;
    }

    int body_len = 0;
    const uint8_t *enc = ble_debug_rx_buf(ref.idx);
    if (enc) {
        body_len = decode_frame(enc, ref.len, buf, cap);
    } else {
        FMRB_LOGW(TAG, "bad buffer index %u", (unsigned)ref.idx);
    }

    // The buffer goes back even when the frame was rejected, otherwise the
    // pool drains and the service stops receiving.
    fmrb_queue_send(s_free_q, &ref, 0);
    return body_len;
}

static fmrb_err_t ble_send(const uint8_t *body, size_t len) {
    if (!ble_debug_link_ready()) {
        return FMRB_ERR_INVALID_STATE;
    }
    if (len > FMRB_DEBUG_MAX_FRAME) {
        FMRB_LOGE(TAG, "body too large (%zu)", len);
        return FMRB_ERR_INVALID_PARAM;
    }

    s_plain[0] = (uint8_t)(len >> 8);
    s_plain[1] = (uint8_t)len;
    memcpy(s_plain + 2, body, len);

    size_t plain_len = 2 + len;
    uint32_t crc = ble_framing_crc32(s_plain, plain_len);
    s_plain[plain_len++] = (uint8_t)(crc >> 24);
    s_plain[plain_len++] = (uint8_t)(crc >> 16);
    s_plain[plain_len++] = (uint8_t)(crc >> 8);
    s_plain[plain_len++] = (uint8_t)crc;

    size_t enc_len = ble_framing_cobs_encode(s_plain, plain_len, s_enc, sizeof(s_enc));
    if (enc_len == 0 || enc_len >= sizeof(s_enc)) {
        FMRB_LOGE(TAG, "COBS encode failed (plain %zu bytes)", plain_len);
        return FMRB_ERR_FAILED;
    }
    s_enc[enc_len++] = BLE_FRAMING_DELIM;

    return ble_debug_send(s_enc, enc_len);
}

static bool ble_connected(void) {
    return ble_debug_link_ready();
}

static void ble_close_client(void) {
    if (!s_ready_q) return;
    // Drop anything received but not yet decoded so a new session starts clean.
    ble_dbg_frame_ref_t ref;
    while (fmrb_queue_receive(s_ready_q, &ref, 0) == FMRB_TRUE) {
        fmrb_queue_send(s_free_q, &ref, 0);
    }
}

const fmrb_debug_transport_ops_t fmrb_debug_transport_ble = {
    .init         = ble_init,
    .poll         = ble_poll,
    .send         = ble_send,
    .connected    = ble_connected,
    .close_client = ble_close_client,
};
