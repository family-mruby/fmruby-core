/*
 * Copyright (c) 2022 ZettaScale Technology
 *
 * Derived from zenoh-pico 1.10.1, src/link/transport/upper/ws_emscripten.c,
 * whose notice was:
 *
 *   Copyright (c) 2022 ZettaScale Technology
 *
 *   This program and the accompanying materials are made available under the
 *   terms of the Eclipse Public License 2.0 which is available at
 *   http://www.eclipse.org/legal/epl-2.0, or the Apache License, Version 2.0
 *   which is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 *   SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
 *
 *   Contributors:
 *     ZettaScale Zenoh Team, <zenoh@zettascale.tech>
 *
 * Of zenoh-pico's two licenses, this file is used under the Apache License,
 * Version 2.0. It has been rewritten (see below); the function names and the
 * shape of the link are upstream's.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * zenoh-pico's WebSocket link for the Family mruby browser build, compiled in
 * place of the upstream file (doc/ruby_asterism/report/b1.md).
 *
 * The problem it solves: a browser WebSocket lives on the page's main thread
 * and is asynchronous, while zenoh-pico reads and writes synchronously, from
 * a FreeRTOS task that runs in a Web Worker (PROXY_TO_PTHREAD), on a
 * cooperative scheduler where a task that waits without a FreeRTOS wait holds
 * up every other task.
 *
 * Upstream goes through Emscripten's socket emulation (SOCKFS) and sleeps
 * with emscripten_sleep, i.e. it needs ASYNCIFY. In a pthread build SOCKFS
 * works without it -- every socket call is proxied synchronously to the main
 * thread -- and that was this file's first version. Measured, it was not good
 * enough: each poll made several proxied calls (about 1 ms per poll), and a
 * proxied call waits for the main thread, so a page whose main thread was busy
 * (a long JavaScript task) stopped the whole machine for seconds, and the
 * session lost its lease.
 *
 * So the WebSocket is owned by JavaScript on the main thread and talks to
 * the worker through two rings in the shared wasm memory:
 *
 * - receive: the WebSocket's onmessage appends [u32 length][bytes] to the rx
 *   ring and advances its write count. A read in the worker is a plain
 *   memory read: no call to the main thread, never a wait.
 * - send: the worker appends [u32 length][bytes] to the tx ring and asks the
 *   main thread, asynchronously, to drain it (WebSocket.send). Nothing waits
 *   for that; a full ring waits (yielding) up to the send timeout.
 * - state: connecting / open / closed, set by the WebSocket's events. open()
 *   waits for it (yielding) up to PICORUBY_ZENOH_CONNECT_TIMEOUT_MS.
 *
 * Only opening and closing are synchronous calls to the main thread.
 *
 * Connecting again (doc/ruby_asterism, G1.5b): zenoh-pico opens a lost
 * connection again by itself, a try a second. Here the tries are spaced out
 * after failures (1, 2, 4, 8, 16 s, then every 30 s; a try in between fails
 * at once, without a WebSocket), so a router that is away is not knocked on
 * every second. A page that came with a pass (the router door's) gives up
 * after ZP_WS_MAX_FAILS failed tries in a row -- its pass has most likely
 * closed -- and tells the page (Module.zenohGaveUp), which says so.
 *
 * Rooms (doc/ruby_asterism, G2): a page opened with a room's key
 * (Module.zenohRoom, read by web/main.js from the link's #room=) goes to the
 * rooms' door instead, wss://<router>/room?t=<key> (on wss:// only, like the
 * pass, so the key never travels in clear), and gives up like a page with a
 * pass when the tries keep failing (the room has closed or ended). Every
 * session of the page gets the room's key space room/<slot> as its
 * namespace: picoruby-asterism-zenoh asks picoruby_zenoh_default_namespace()
 * (defined below from Module.zenohNamespace) when a session is opened
 * without one, so the apps use their usual keys inside the room.
 *
 * The link is a datagram link to zenoh-pico: one WebSocket message carries
 * one batch, as zenohd's ws/ listener expects. The rest of the contract is
 * picoruby-asterism-zenoh's (include/picoruby_zenoh_link.h): the polled read
 * never waits, the handshake read waits up to
 * PICORUBY_ZENOH_READ_EXACT_TIMEOUT_MS, and zp_tcp_socket_closed() reports a
 * closed or failed WebSocket so the gem closes the session at once.
 */
#include "zenoh-pico/config.h"

#if defined(ZENOH_EMSCRIPTEN) && Z_FEATURE_LINK_WS == 1

#include <emscripten/emscripten.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "picoruby_zenoh_link.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/transport/ws.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/pointers.h"
#include "zp_emscripten_wait.h"

/* Failed opens in a row before a page with a pass gives up, and the longest
 * pause between two tries (ms). */
#define ZP_WS_MAX_FAILS 10
#define ZP_WS_MAX_PAUSE_MS 30000

/* Sessions open at once (each has one WebSocket). */
#define ZP_WS_SLOTS 4
/* Ring sizes, powers of two (the counts wrap at 2^32). The receive ring holds
 * what arrives between two polls: a few camera frames, with room to spare. */
#define ZP_WS_RX_CAP (256u * 1024u)
#define ZP_WS_TX_CAP (64u * 1024u)

enum { ZP_WS_FREE = 0, ZP_WS_CONNECTING = 1, ZP_WS_OPEN = 2, ZP_WS_CLOSED = 3 };

/* Shared with the JavaScript below by word offset: keep the order. */
typedef struct {
    int32_t state;   /* 0 */
    uint32_t rx_wr;  /* 1: bytes written by the main thread */
    uint32_t rx_rd;  /* 2: bytes consumed by the worker */
    uint32_t rx_cap; /* 3 */
    uint32_t rx_buf; /* 4: address */
    uint32_t tx_wr;  /* 5: bytes written by the worker */
    uint32_t tx_rd;  /* 6: bytes sent by the main thread */
    uint32_t tx_cap; /* 7 */
    uint32_t tx_buf; /* 8: address */
} zp_ws_ring_t;

/* Static: the main thread may still look at a slot after the worker is done
 * with it (an asynchronous drain), so the memory is never given back. */
static zp_ws_ring_t s_ring[ZP_WS_SLOTS];
static uint8_t s_rx[ZP_WS_SLOTS][ZP_WS_RX_CAP];
static uint8_t s_tx[ZP_WS_SLOTS][ZP_WS_TX_CAP];
static bool s_used[ZP_WS_SLOTS];

static int32_t zp_ws_state(int slot) { return __atomic_load_n(&s_ring[slot].state, __ATOMIC_ACQUIRE); }

static void zp_ws_set_state(int slot, int32_t st) { __atomic_store_n(&s_ring[slot].state, st, __ATOMIC_RELEASE); }

static bool zp_ws_valid(int slot) { return slot >= 0 && slot < ZP_WS_SLOTS && s_used[slot]; }

/* The spacing of the tries (see the top). Touched by the worker only. */
static uint32_t s_fails;
static uint32_t s_pause_ms;
static z_clock_t s_failed_at;
static bool s_gave_up;

/* Main thread: create the WebSocket and wire its events to the ring. The URL
 * is ASCII (the locator's characters are checked by zenoh-pico and the page). */
static int zp_ws_js_open(int slot, const char *url, zp_ws_ring_t *ring) {
    return MAIN_THREAD_EM_ASM_INT(
        {
            const slot = $0;
            const base = $2;
            let u8 = new Uint8Array(wasmMemory.buffer);
            let url = '';
            for (let p = $1; u8[p]; p++) url += String.fromCharCode(u8[p]);
            const zp = Module['zpws'] || (Module['zpws'] = {});
            // ws:// or wss:// (doc/ruby_asterism, B2). The locator is ws/
            // either way (zenoh-pico has no wss); the scheme follows the page.
            // A page served over https may not open ws:// -- the constructor
            // throws -- except to a loopback address, which Chrome and Firefox
            // both count as secure: a router on this computer is reached
            // without TLS, anything else through a TLS-terminating proxy.
            if (typeof location !== 'undefined' && location.protocol === 'https:') {
                let host;
                try { host = new URL(url).hostname.toLowerCase(); } catch (e) { host = String(); }
                const loopback = host === 'localhost' || host.endsWith('.localhost') ||
                                 /^127[.][0-9]+[.][0-9]+[.][0-9]+$/.test(host) || host === '[::1]';
                if (!loopback) {
                    url = 'wss' + url.slice(2);
                    // A room's key (G2, web/main.js): the rooms' door at
                    // /room checks it; no pass then. Never logged.
                    const room = Module['zenohRoom'];
                    if (room) {
                        url = url.replace(/[/]$/, '') + '/room?t=' + encodeURIComponent(room);
                    }
                    // The pass from the page's link (#zenoh_pass=, read by
                    // web/index.html and web/main.js): the router door checks
                    // it (doc/ruby_asterism, G1). Only on wss://, so it never
                    // travels in clear; never logged.
                    const pass = room ? '' : Module['zenohPass'];
                    if (pass) url += (url.indexOf('?') < 0 ? '?' : '&') + 'pass=' + encodeURIComponent(pass);
                    // The page's own key (G1.5b, web/main.js): the door
                    // lets this page, and no other, connect again with the
                    // same pass while the pass is open.
                    const page = Module['zenohPage'];
                    if (pass && page) url += '&page=' + encodeURIComponent(page);
                }
            }
            let ws;
            try {
                ws = new WebSocket(url);
            } catch (e) {
                return -1;
            }
            ws.binaryType = 'arraybuffer';
            zp[slot] = ws;
            const words = () => new Int32Array(wasmMemory.buffer, base, 9);
            ws.onopen = () => Atomics.store(words(), 0, 2);
            ws.onclose = ws.onerror = () => Atomics.store(words(), 0, 3);
            ws.onmessage = (ev) => {
                const h = words();
                const data = new Uint8Array(ev.data);
                const n = data.length;
                const cap = h[3] >>> 0;
                const buf = h[4] >>> 0;
                const wr = Atomics.load(h, 1) >>> 0;
                const rd = Atomics.load(h, 2) >>> 0;
                if (cap - ((wr - rd) >>> 0) < n + 4) {
                    // Losing a message would break the session anyway.
                    Atomics.store(h, 0, 3);
                    try { ws.close(); } catch (e) {}
                    return;
                }
                const m = new Uint8Array(wasmMemory.buffer);
                for (let i = 0; i < 4; i++) m[buf + ((wr + i) & (cap - 1))] = (n >>> (8 * i)) & 255;
                const pos = (wr + 4) & (cap - 1);
                const first = Math.min(n, cap - pos);
                m.set(data.subarray(0, first), buf + pos);
                if (first < n) m.set(data.subarray(first), buf);
                Atomics.store(h, 1, (wr + 4 + n) | 0);
            };
            return 0;
        },
        slot, url, ring);
}

/* Main thread, asynchronously: send what the tx ring holds. */
static void zp_ws_js_kick(int slot, zp_ws_ring_t *ring) {
    MAIN_THREAD_ASYNC_EM_ASM(
        {
            const ws = (Module['zpws'] || {})[$0];
            const h = new Int32Array(wasmMemory.buffer, $1, 9);
            if (!ws) return;
            if (ws.readyState !== 1) {
                if (ws.readyState > 1) Atomics.store(h, 0, 3);
                return;
            }
            const m = new Uint8Array(wasmMemory.buffer);
            const cap = h[7] >>> 0;
            const buf = h[8] >>> 0;
            const wr = Atomics.load(h, 5) >>> 0;
            let rd = Atomics.load(h, 6) >>> 0;
            while (((wr - rd) >>> 0) >= 4) {
                let n = 0;
                for (let i = 0; i < 4; i++) n |= m[buf + ((rd + i) & (cap - 1))] << (8 * i);
                n >>>= 0;
                const out = new Uint8Array(n);
                const pos = (rd + 4) & (cap - 1);
                const first = Math.min(n, cap - pos);
                out.set(m.subarray(buf + pos, buf + pos + first));
                if (first < n) out.set(m.subarray(buf, buf + n - first), first);
                try {
                    ws.send(out);
                } catch (e) {
                    Atomics.store(h, 0, 3);
                    return;
                }
                rd = (rd + 4 + n) >>> 0;
            }
            Atomics.store(h, 6, rd | 0);
        },
        slot, ring);
}

/* Main thread: close the WebSocket and forget it. Synchronous, so no event of
 * the old connection touches the slot once it is reused. */
static void zp_ws_js_close(int slot) {
    MAIN_THREAD_EM_ASM(
        {
            const zp = Module['zpws'] || {};
            const ws = zp[$0];
            if (!ws) return;
            ws.onopen = ws.onclose = ws.onerror = ws.onmessage = null;
            try { ws.close(); } catch (e) {}
            delete zp[$0];
        },
        slot);
}

static void zp_ws_ring_copy_out(const uint8_t *buf, uint32_t cap, uint32_t at, uint8_t *dst, size_t n) {
    uint32_t pos = at & (cap - 1);
    size_t first = (n < cap - pos) ? n : cap - pos;
    memcpy(dst, buf + pos, first);
    if (first < n) {
        memcpy(dst + first, buf, n - first);
    }
}

static void zp_ws_ring_copy_in(uint8_t *buf, uint32_t cap, uint32_t at, const uint8_t *src, size_t n) {
    uint32_t pos = at & (cap - 1);
    size_t first = (n < cap - pos) ? n : cap - pos;
    memcpy(buf + pos, src, first);
    if (first < n) {
        memcpy(buf, src + first, n - first);
    }
}

/* One received message into ptr. SIZE_MAX: nothing queued. 0: the WebSocket
 * has closed (and nothing is queued). */
static size_t zp_ws_read_now(int slot, uint8_t *ptr, size_t len) {
    if (!zp_ws_valid(slot)) {
        return 0;
    }
    zp_ws_ring_t *r = &s_ring[slot];
    for (;;) {
        uint32_t wr = __atomic_load_n(&r->rx_wr, __ATOMIC_ACQUIRE);
        uint32_t rd = r->rx_rd;
        if (wr - rd < 4) {
            return zp_ws_state(slot) == ZP_WS_CLOSED ? 0 : SIZE_MAX;
        }
        uint8_t hdr[4];
        zp_ws_ring_copy_out(s_rx[slot], r->rx_cap, rd, hdr, 4);
        uint32_t n = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
        bool fits = n <= len;
        if (fits) {
            zp_ws_ring_copy_out(s_rx[slot], r->rx_cap, rd + 4, ptr, n);
        }
        __atomic_store_n(&r->rx_rd, rd + 4 + n, __ATOMIC_RELEASE);
        if (fits && n > 0) {
            return n;
        }
        if (!fits) {
            /* Larger than the batch zenoh-pico asked for: cannot happen with a
             * router that honours the negotiated size. Drop it. */
            _Z_ERROR("ws: dropped a %u-byte message (buffer %u)", (unsigned)n, (unsigned)len);
        }
    }
}

/* The handshake read: wait for one message. */
static size_t zp_ws_read_wait(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len) {
    uint32_t limit = sock._ws._tout > PICORUBY_ZENOH_READ_EXACT_TIMEOUT_MS ? sock._ws._tout
                                                                            : PICORUBY_ZENOH_READ_EXACT_TIMEOUT_MS;
    z_clock_t start = z_clock_now();
    for (;;) {
        size_t rb = zp_ws_read_now(sock._ws._fd, ptr, len);
        if (rb != SIZE_MAX) {
            return rb;
        }
        if (z_clock_elapsed_ms(&start) >= limit) {
            return SIZE_MAX;
        }
        zp_ems_wait_ms(1);
    }
}

static size_t zp_ws_write(const _z_sys_net_socket_t sock, const uint8_t *ptr, size_t len) {
    int slot = sock._ws._fd;
    if (!zp_ws_valid(slot) || zp_ws_state(slot) != ZP_WS_OPEN) {
        return SIZE_MAX;
    }
    zp_ws_ring_t *r = &s_ring[slot];
    if (len + 4 > r->tx_cap) {
        return SIZE_MAX;
    }
    z_clock_t start = z_clock_now();
    for (;;) {
        uint32_t rd = __atomic_load_n(&r->tx_rd, __ATOMIC_ACQUIRE);
        uint32_t wr = r->tx_wr;
        if (r->tx_cap - (wr - rd) >= len + 4) {
            uint8_t hdr[4] = {(uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24)};
            zp_ws_ring_copy_in(s_tx[slot], r->tx_cap, wr, hdr, 4);
            zp_ws_ring_copy_in(s_tx[slot], r->tx_cap, wr + 4, ptr, len);
            __atomic_store_n(&r->tx_wr, wr + 4 + (uint32_t)len, __ATOMIC_RELEASE);
            zp_ws_js_kick(slot, r);
            return len;
        }
        /* The main thread has not caught up: give it time, yielding. */
        if (zp_ws_state(slot) != ZP_WS_OPEN || z_clock_elapsed_ms(&start) >= PICORUBY_ZENOH_SEND_TIMEOUT_MS) {
            zp_ws_set_state(slot, ZP_WS_CLOSED);
            return SIZE_MAX;
        }
        zp_ems_wait_ms(1);
    }
}

/* Main thread: whether the page came with a pass, and telling it that the
 * tries stopped. */
static bool zp_ws_js_has_pass(void) {
    return MAIN_THREAD_EM_ASM_INT({ return (Module['zenohPass'] || Module['zenohRoom']) ? 1 : 0; }) != 0;
}

/* The key space of the page's room (G2), for picoruby-asterism-zenoh (its
 * weak default returns NULL: no namespace). Read once from
 * Module.zenohNamespace on the main thread into a static buffer; web/main.js
 * sets it to room/<slot> from the room's key, or not at all. */
const char *picoruby_zenoh_default_namespace(void);

static char s_namespace[64];
static int s_namespace_read; /* 0: not yet, 1: read */

const char *picoruby_zenoh_default_namespace(void) {
    if (!s_namespace_read) {
        MAIN_THREAD_EM_ASM(
            {
                const ns = String(Module['zenohNamespace'] || '');
                const u8 = new Uint8Array(wasmMemory.buffer);
                const n = ns.length < $1 ? ns.length : 0; /* too long: none (the binding would refuse it) */
                for (let i = 0; i < n; i++) u8[$0 + i] = ns.charCodeAt(i) & 0x7f;
                u8[$0 + n] = 0;
            },
            s_namespace, (int)sizeof(s_namespace));
        s_namespace_read = 1;
    }
    return s_namespace[0] != '\0' ? s_namespace : NULL;
}

static void zp_ws_js_gave_up(int fails) {
    MAIN_THREAD_ASYNC_EM_ASM(
        {
            if (typeof Module['zenohGaveUp'] === 'function') Module['zenohGaveUp']($0);
        },
        fails);
}

static z_result_t zp_ws_open_once(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, uint32_t tout);

static z_result_t zp_ws_open(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, uint32_t tout) {
    sock->_ws._fd = -1;
    if (s_gave_up || (s_fails > 0 && z_clock_elapsed_ms(&s_failed_at) < s_pause_ms)) {
        return _Z_ERR_GENERIC; /* not yet (or not any more): no WebSocket */
    }
    z_result_t ret = zp_ws_open_once(sock, rep, tout);
    if (ret == _Z_RES_OK) {
        if (s_fails > 0) {
            _Z_INFO("ws: connected again after %u failed tries", (unsigned)s_fails);
        }
        s_fails = 0;
        s_pause_ms = 0;
        return ret;
    }
    s_fails++;
    s_failed_at = z_clock_now();
    s_pause_ms = s_pause_ms == 0 ? 1000 : s_pause_ms * 2;
    if (s_pause_ms > ZP_WS_MAX_PAUSE_MS) {
        s_pause_ms = ZP_WS_MAX_PAUSE_MS;
    }
    if (s_fails >= ZP_WS_MAX_FAILS && zp_ws_js_has_pass()) {
        s_gave_up = true;
        zp_ws_js_gave_up((int)s_fails);
    }
    return ret;
}

static z_result_t zp_ws_open_once(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, uint32_t tout) {
    sock->_ws._fd = -1;
    sock->_ws._tout = tout;
    int slot = -1;
    for (int i = 0; i < ZP_WS_SLOTS; i++) {
        if (!s_used[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0 || rep._url == NULL) {
        _Z_ERROR_LOG(_Z_ERR_GENERIC);
        return _Z_ERR_GENERIC;
    }
    zp_ws_ring_t *r = &s_ring[slot];
    memset(r, 0, sizeof(*r));
    r->rx_cap = ZP_WS_RX_CAP;
    r->rx_buf = (uint32_t)(uintptr_t)s_rx[slot];
    r->tx_cap = ZP_WS_TX_CAP;
    r->tx_buf = (uint32_t)(uintptr_t)s_tx[slot];
    zp_ws_set_state(slot, ZP_WS_CONNECTING);
    s_used[slot] = true;
    if (zp_ws_js_open(slot, rep._url, r) != 0) {
        s_used[slot] = false;
        _Z_ERROR_LOG(_Z_ERR_GENERIC);
        return _Z_ERR_GENERIC;
    }
    z_clock_t start = z_clock_now();
    for (;;) {
        int32_t st = zp_ws_state(slot);
        if (st == ZP_WS_OPEN) {
            sock->_ws._fd = slot;
            return _Z_RES_OK;
        }
        if (st == ZP_WS_CLOSED || z_clock_elapsed_ms(&start) >= PICORUBY_ZENOH_CONNECT_TIMEOUT_MS) {
            break;
        }
        zp_ems_wait_ms(1);
    }
    zp_ws_js_close(slot);
    s_used[slot] = false;
    _Z_ERROR_LOG(_Z_ERR_GENERIC);
    return _Z_ERR_GENERIC;
}

static void zp_ws_close(_z_sys_net_socket_t *sock) {
    int slot = sock->_ws._fd;
    if (zp_ws_valid(slot)) {
        zp_ws_js_close(slot);
        zp_ws_set_state(slot, ZP_WS_FREE);
        s_used[slot] = false;
    }
    sock->_ws._fd = -1;
}

bool zp_tcp_socket_closed(const _z_sys_net_socket_t *sock) {
    int slot = sock->_ws._fd;
    return !zp_ws_valid(slot) || zp_ws_state(slot) == ZP_WS_CLOSED;
}

z_result_t _z_ws_endpoint_init(_z_sys_net_endpoint_t *ep, const _z_string_t *address) {
    ep->_url = NULL;
    char *host = _z_endpoint_parse_host(address);
    char *port = _z_endpoint_parse_port(address);
    z_result_t ret = _Z_RES_OK;
    if ((host == NULL) || (port == NULL)) {
        ret = _Z_ERR_CONFIG_LOCATOR_INVALID;
    } else {
        /* An IPv6 literal comes back without its brackets. */
        bool v6 = strchr(host, ':') != NULL;
        size_t n = strlen(host) + strlen(port) + 16;
        ep->_url = (char *)z_malloc(n);
        if (ep->_url == NULL) {
            ret = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
        } else {
            snprintf(ep->_url, n, v6 ? "ws://[%s]:%s/" : "ws://%s:%s/", host, port);
        }
    }
    z_free(host);
    z_free(port);
    return ret;
}

void _z_ws_endpoint_clear(_z_sys_net_endpoint_t *ep) {
    z_free(ep->_url);
    ep->_url = NULL;
}

z_result_t _z_ws_transport_open(_z_ws_socket_t *sock, uint32_t tout) { return zp_ws_open(&sock->_sock, sock->_rep, tout); }

/* A page cannot listen. */
z_result_t _z_ws_transport_listen(_z_ws_socket_t *sock) {
    _ZP_UNUSED(sock);
    _Z_ERROR_LOG(_Z_ERR_GENERIC);
    return _Z_ERR_GENERIC;
}

void _z_ws_transport_close(_z_ws_socket_t *sock) { zp_ws_close(&sock->_sock); }

size_t _z_ws_transport_read(const _z_ws_socket_t *sock, uint8_t *ptr, size_t len) {
    return zp_ws_read_wait(sock->_sock, ptr, len);
}

size_t _z_ws_transport_read_exact(const _z_ws_socket_t *sock, uint8_t *ptr, size_t len) {
    size_t n = 0;
    uint8_t *pos = &ptr[0];
    do {
        size_t rb = zp_ws_read_wait(sock->_sock, pos, len - n);
        if ((rb == SIZE_MAX) || (rb == 0)) {
            return rb;
        }
        n = n + rb;
        pos = _z_ptr_u8_offset(pos, (ptrdiff_t)rb);
    } while (n != len);
    return n;
}

size_t _z_ws_transport_write(const _z_ws_socket_t *sock, const uint8_t *ptr, size_t len) {
    return zp_ws_write(sock->_sock, ptr, len);
}

/* The polled read. A closed WebSocket reads as "nothing yet" here: on a
 * datagram link zenoh-pico would try to decode a 0-byte message, while the
 * gem notices the closed socket right after the poll (zp_tcp_socket_closed). */
size_t _z_ws_transport_read_socket(const _z_sys_net_socket_t socket, uint8_t *ptr, size_t len) {
    size_t rb = zp_ws_read_now(socket._ws._fd, ptr, len);
    return rb == 0 ? SIZE_MAX : rb;
}

#else
typedef int _zp_ws_emscripten_family_mruby_disabled_t;
#endif
