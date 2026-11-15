/*
 * A MessagePack map written straight into one message to the kernel, for the
 * app boundary's flat functions that are a command (FmrbAudio's, FmrbApp's
 * requests). The bytes are what the msgpack gem writes for the same Hash with
 * String keys (fixstr/str8/str16, the narrowest integer), so the kernel cannot
 * tell them from what the Ruby wrappers sent before (doc/app_boundary W4b-2,
 * W4b-3), and no engine builds a Hash for them. Internal to main/app.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "fmrb_app.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_kernel.h"
#include "fmrb_gen/fmrb_boundary.h"

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
    bool over;
} mp_out_t;

static inline void put(mp_out_t *o, const void *p, size_t n)
{
    if (o->over || o->len + n > o->cap) {
        o->over = true;
        return;
    }
    memcpy(o->buf + o->len, p, n);
    o->len += n;
}

static inline void put_byte(mp_out_t *o, uint8_t b)
{
    put(o, &b, 1);
}

// A String, as the msgpack gem writes one (fixstr, str8, str16).
static inline void put_str(mp_out_t *o, const char *s, size_t n)
{
    if (n < 32) {
        put_byte(o, (uint8_t)(0xa0 | n));
    } else if (n < 256) {
        uint8_t h[2] = { 0xd9, (uint8_t)n };
        put(o, h, 2);
    } else if (n < 65536) {
        uint8_t h[3] = { 0xda, (uint8_t)(n >> 8), (uint8_t)n };
        put(o, h, 3);
    } else {
        o->over = true;
        return;
    }
    put(o, s, n);
}

static inline void put_key(mp_out_t *o, const char *key)
{
    put_str(o, key, strlen(key));
}

static inline void put_bool(mp_out_t *o, bool v)
{
    put_byte(o, v ? 0xc3 : 0xc2);
}

static inline void put_nil(mp_out_t *o)
{
    put_byte(o, 0xc0);
}

// An integer in the narrowest form (the width msgpack-c chooses).
static inline void put_int(mp_out_t *o, int32_t v)
{
    if (v >= 0) {
        if (v < 128) {
            put_byte(o, (uint8_t)v);
        } else if (v < 256) {
            uint8_t h[2] = { 0xcc, (uint8_t)v };
            put(o, h, 2);
        } else if (v < 65536) {
            uint8_t h[3] = { 0xcd, (uint8_t)(v >> 8), (uint8_t)v };
            put(o, h, 3);
        } else {
            uint8_t h[5] = { 0xce, (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
            put(o, h, 5);
        }
    } else if (v >= -32) {
        put_byte(o, (uint8_t)v);
    } else if (v >= INT8_MIN) {
        uint8_t h[2] = { 0xd0, (uint8_t)v };
        put(o, h, 2);
    } else if (v >= INT16_MIN) {
        uint8_t h[3] = { 0xd1, (uint8_t)((uint16_t)v >> 8), (uint8_t)v };
        put(o, h, 3);
    } else {
        uint32_t u = (uint32_t)v;
        uint8_t h[5] = { 0xd2, (uint8_t)(u >> 24), (uint8_t)(u >> 16), (uint8_t)(u >> 8), (uint8_t)u };
        put(o, h, 5);
    }
}

typedef struct {
    fmrb_msg_t msg;
    mp_out_t out;
} fmrb_bnd_cmd_t;

// A message of the given type to the kernel: a map of npairs pairs whose
// first is "cmd" => cmd. NULL outside an app.
static inline fmrb_app_task_context_t *fmrb_bnd_cmd_begin(fmrb_bnd_cmd_t *c, fmrb_msg_type_t type,
                                                          const char *cmd, int npairs)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return NULL;
    }
    memset(&c->msg, 0, offsetof(fmrb_msg_t, data));
    c->msg.type = type;
    c->msg.src_pid = ctx->app_id;
    c->out = (mp_out_t){ .buf = c->msg.data, .len = 0, .cap = FMRB_MAX_MSG_PAYLOAD_SIZE, .over = false };
    put_byte(&c->out, (uint8_t)(0x80 | npairs));
    put_key(&c->out, "cmd");
    put_key(&c->out, cmd);
    return ctx;
}

// Sends it. Too big for one message is an argument error; a full queue drops
// it with a line in the log (as the Ruby wrappers did: not an error).
static inline int32_t fmrb_bnd_cmd_send(fmrb_bnd_cmd_t *c, const char *tag, const char *cmd)
{
    if (c->out.over) {
        FMRB_LOGE(tag, "%s: the command does not fit one message (%d bytes)", cmd,
                  FMRB_MAX_MSG_PAYLOAD_SIZE);
        return FMRB_BOUNDARY_ERR_ARG;
    }
    c->msg.size = (uint32_t)c->out.len;
    if (fmrb_msg_send(PROC_ID_KERNEL, &c->msg, 1000) != FMRB_OK) {
        FMRB_LOGW(tag, "%s: the kernel's queue did not take it; dropped", cmd);
    }
    return 0;
}
