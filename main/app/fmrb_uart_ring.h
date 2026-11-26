/*
 * The receive ring of an app's UART (main/app/fmrb_boundary_uart.c): bytes
 * the driver had are moved here when the app calls, and the app reads from
 * here. libc only, so the host test (test/app_uart) compiles it as it is.
 *
 * The size is a power of two; head and tail run free (their difference is
 * the count), so a full ring holds all `size` bytes. A byte that does not
 * fit is the newest one and is dropped (the caller counts it): what was
 * already there is never overwritten.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t *buf;
    uint32_t size;  // a power of two
    uint32_t head;  // the next byte to read
    uint32_t tail;  // where the next byte goes
} fmrb_uart_ring_t;

static inline bool fmrb_uart_ring_size_ok(uint32_t size)
{
    return size != 0 && (size & (size - 1)) == 0;
}

static inline void fmrb_uart_ring_init(fmrb_uart_ring_t *r, uint8_t *buf, uint32_t size)
{
    r->buf = buf;
    r->size = size;
    r->head = 0;
    r->tail = 0;
}

static inline uint32_t fmrb_uart_ring_used(const fmrb_uart_ring_t *r)
{
    return r->tail - r->head;
}

static inline uint32_t fmrb_uart_ring_free(const fmrb_uart_ring_t *r)
{
    return r->size - fmrb_uart_ring_used(r);
}

static inline void fmrb_uart_ring_clear(fmrb_uart_ring_t *r)
{
    r->head = r->tail;
}

// Add up to n bytes at the end; returns how many went in (the rest are the
// ones to drop).
static inline uint32_t fmrb_uart_ring_push(fmrb_uart_ring_t *r, const uint8_t *src, uint32_t n)
{
    uint32_t room = fmrb_uart_ring_free(r);
    if (n > room) {
        n = room;
    }
    uint32_t mask = r->size - 1;
    for (uint32_t i = 0; i < n; i++) {
        r->buf[(r->tail + i) & mask] = src[i];
    }
    r->tail += n;
    return n;
}

// Copy n bytes from the front (n <= used) without taking them.
static inline void fmrb_uart_ring_peek(const fmrb_uart_ring_t *r, uint8_t *dst, uint32_t n)
{
    uint32_t mask = r->size - 1;
    uint32_t at = r->head & mask;
    uint32_t first = r->size - at;
    if (first > n) {
        first = n;
    }
    memcpy(dst, r->buf + at, first);
    memcpy(dst + first, r->buf, n - first);
}

// Take n bytes from the front (n <= used).
static inline void fmrb_uart_ring_drop(fmrb_uart_ring_t *r, uint32_t n)
{
    r->head += n;
}

// Put one byte back in front of the others; false when the ring is full.
static inline bool fmrb_uart_ring_unshift(fmrb_uart_ring_t *r, uint8_t byte)
{
    if (fmrb_uart_ring_free(r) == 0) {
        return false;
    }
    r->head--;
    r->buf[r->head & (r->size - 1)] = byte;
    return true;
}

// The count of bytes up to and including the first `byte`, 0 when there is
// none.
static inline uint32_t fmrb_uart_ring_find(const fmrb_uart_ring_t *r, uint8_t byte)
{
    uint32_t used = fmrb_uart_ring_used(r);
    uint32_t mask = r->size - 1;
    for (uint32_t i = 0; i < used; i++) {
        if (r->buf[(r->head + i) & mask] == byte) {
            return i + 1;
        }
    }
    return 0;
}
