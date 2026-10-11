/*
 * Copyright (c) 2022 ZettaScale Technology
 *
 * Derived from zenoh-pico 1.10.1, src/system/emscripten/system.c, whose
 * notice was:
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
 * Version 2.0. It has been modified (see below).
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * zenoh-pico's system layer for the Family mruby browser build, compiled in
 * place of the upstream file (doc/ruby_asterism/report/b1.md). Changes:
 *
 * - Sleeping does not use emscripten_sleep(), which requires ASYNCIFY (see
 *   zp_emscripten_wait.h for why this build needs no event-loop turn).
 * - z_random_fill() fills the whole buffer (upstream writes every byte to
 *   the first one).
 * - Single-threaded only (the gem's configuration): the task/mutex/condvar
 *   part of the upstream file is left out.
 */
#include <emscripten/html5.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/system/platform.h"
#include "zp_emscripten_wait.h"

#if Z_FEATURE_MULTI_THREAD == 1
#error "the Family mruby zenoh-pico Emscripten layer is single-threaded"
#endif

/*------------------ Random ------------------*/
uint8_t z_random_u8(void) { return (uint8_t)(emscripten_random() * 255.0); }

uint16_t z_random_u16(void) { return (uint16_t)(emscripten_random() * 65535.0); }

uint32_t z_random_u32(void) { return (uint32_t)(emscripten_random() * 4294967295.0); }

uint64_t z_random_u64(void) { return ((uint64_t)z_random_u32() << 32) | z_random_u32(); }

void z_random_fill(void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        p[i] = z_random_u8();
    }
}

/*------------------ Memory ------------------*/
void *z_malloc(size_t size) { return malloc(size); }

void *z_realloc(void *ptr, size_t size) { return realloc(ptr, size); }

void z_free(void *ptr) { free(ptr); }

/*------------------ Sleep ------------------*/
z_result_t z_sleep_us(size_t time) {
    zp_ems_wait_ms((uint32_t)((time / 1000) + (time % 1000 == 0 ? 0 : 1)));
    return 0;
}

z_result_t z_sleep_ms(size_t time) {
    zp_ems_wait_ms((uint32_t)time);
    return 0;
}

z_result_t z_sleep_s(size_t time) {
    z_time_t start = z_time_now();
    while (z_time_elapsed_s(&start) < time) {
        z_sleep_ms(1000);
    }
    return 0;
}

unsigned long z_time_elapsed_ms_since(z_time_t *time, z_time_t *epoch) {
    unsigned long elapsed = *time > *epoch ? (unsigned long)(*time - *epoch) : 0;
    return elapsed;
}

/*------------------ Instant ------------------*/
z_clock_t z_clock_now(void) { return z_time_now(); }

unsigned long zp_clock_elapsed_us_since(z_clock_t *instant, z_clock_t *epoch) {
    return z_time_elapsed_ms_since(instant, epoch) * 1000;
}
unsigned long zp_clock_elapsed_ms_since(z_clock_t *instant, z_clock_t *epoch) {
    return z_time_elapsed_ms_since(instant, epoch);
}
unsigned long zp_clock_elapsed_s_since(z_clock_t *instant, z_clock_t *epoch) {
    return z_time_elapsed_ms_since(instant, epoch) / 1000;
}

unsigned long z_clock_elapsed_us(z_clock_t *instant) { return z_clock_elapsed_ms(instant) * 1000; }

unsigned long z_clock_elapsed_ms(z_clock_t *instant) { return z_time_elapsed_ms(instant); }

unsigned long z_clock_elapsed_s(z_clock_t *instant) { return z_time_elapsed_ms(instant) / 1000; }

void z_clock_advance_us(z_clock_t *clock, unsigned long duration) { *clock += (double)duration / 1000.0; }

void z_clock_advance_ms(z_clock_t *clock, unsigned long duration) { *clock += (double)duration; }

void z_clock_advance_s(z_clock_t *clock, unsigned long duration) { *clock += (double)duration * 1000.0; }

/*------------------ Time ------------------*/
z_time_t z_time_now(void) { return emscripten_get_now(); }

const char *z_time_now_as_str(char *const buf, unsigned long buflen) {
    snprintf(buf, buflen, "%f", z_time_now());
    return buf;
}

unsigned long z_time_elapsed_us(z_time_t *time) { return z_time_elapsed_ms(time) * 1000; }

unsigned long z_time_elapsed_ms(z_time_t *time) {
    z_time_t now = emscripten_get_now();
    return z_time_elapsed_ms_since(&now, time);
}

unsigned long z_time_elapsed_s(z_time_t *time) { return z_time_elapsed_ms(time) / 1000; }

z_result_t _z_get_time_since_epoch(_z_time_since_epoch *t) {
    double date = emscripten_date_now();
    t->secs = (uint32_t)(date / 1000);
    t->nanos = (uint32_t)((date - t->secs * 1000.0) * 1000000);
    return 0;
}
