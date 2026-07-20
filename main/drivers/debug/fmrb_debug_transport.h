// Transport abstraction for the remote debugger. The debugd core speaks in
// bare msgpack bodies; each transport owns its own framing (TCP = u32 BE length
// prefix, BLE = COBS+CRC32 later). Adding BLE later means adding one more ops
// table; debugd never learns the transport's name.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "fmrb_err.h"

typedef struct {
    // Start listening. Called once from the debugd task.
    fmrb_err_t (*init)(void);
    // Poll for one complete msgpack body (framing already stripped).
    // Returns: >0 = body byte length written into buf,
    //           0 = timeout / no complete frame yet,
    //          <0 = transport error (caller should treat as disconnect).
    int (*poll)(uint8_t *buf, size_t cap, uint32_t timeout_ms);
    // Send one msgpack body; the transport prepends its own framing.
    fmrb_err_t (*send)(const uint8_t *body, size_t len);
    // Is a client currently connected?
    bool (*connected)(void);
    // Drop the current client connection (keep listening for a new one).
    void (*close_client)(void);
} fmrb_debug_transport_ops_t;

// Linux TCP transport (fmrb_debug_transport_tcp.c). Linux build only.
extern const fmrb_debug_transport_ops_t fmrb_debug_transport_tcp;

// BLE GATT transport (fmrb_debug_transport_ble.c). ESP32 targets only.
extern const fmrb_debug_transport_ops_t fmrb_debug_transport_ble;
