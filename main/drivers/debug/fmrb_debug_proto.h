// Wire protocol (msgpack) for the PicoRuby remote debugger.
// See doc/vm_remote_debug_protocol.md for the on-wire format.
//
// This layer owns: request decoding (debugd never touches raw msgpack for
// parsing) and the command-name -> enum table, plus small helpers for building
// response/event bodies. Framing (the u32 length prefix) is the transport's
// job; this layer produces/consumes bare msgpack bodies.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <msgpack.h>

#include "fmrb_err.h"

// --- Compile-time limits / config -----------------------------------------
#define FMRB_DEBUG_TCP_PORT        5555     // 0.0.0.0:<port> TCP listen
#define FMRB_DEBUG_MAX_FRAME       4096     // max msgpack body bytes per frame
#define FMRB_DEBUG_MAX_ATTACH      4        // max simultaneously attached VMs
#define FMRB_DEBUG_MAX_BP          16       // breakpoints per attached VM
#define FMRB_DEBUG_PROTO_VER       1
#define FMRB_DEBUG_MAX_FILE        64       // breakpoint/source path buffer
#define FMRB_DEBUG_MAX_SPAWN_PATH  128      // spawn target path buffer

// Message type tags (first array element).
#define FMRB_DBG_MSG_REQUEST   0
#define FMRB_DBG_MSG_RESPONSE  1
#define FMRB_DBG_MSG_EVENT     2

// --- Commands --------------------------------------------------------------
typedef enum {
    DBG_CMD_UNKNOWN = 0,
    DBG_CMD_VERSION,
    DBG_CMD_PS,
    DBG_CMD_ATTACH,
    DBG_CMD_DETACH,
    DBG_CMD_BP_SET,
    DBG_CMD_BP_CLEAR,
    DBG_CMD_PAUSE,
    DBG_CMD_CONTINUE,
    DBG_CMD_STEP_IN,
    DBG_CMD_STEP_OVER,
    DBG_CMD_STEP_OUT,
    DBG_CMD_STACK_TRACE,
    DBG_CMD_FRAME_VARS,
    DBG_CMD_LOG_READ,
    DBG_CMD_KILL,
    DBG_CMD_STOP,
    DBG_CMD_SUSPEND,
    DBG_CMD_RESUME,
    DBG_CMD_SPAWN,
} fmrb_dbg_cmd_t;

// Decoded request. Numeric fields default to 0 / their "unset" sentinel; use
// the have_* flags where "0 vs absent" matters.
typedef struct {
    uint16_t        seq;
    fmrb_dbg_cmd_t  cmd;
    bool            have_pid;
    int             pid;
    int             line;
    int             bp_id;
    int             frame;
    int             max;        // stack_trace max frames
    int             max_lines;  // log_read
    uint32_t        pos;        // log_read read position
    char            file[FMRB_DEBUG_MAX_FILE];
    char            path[FMRB_DEBUG_MAX_SPAWN_PATH];
} fmrb_dbg_req_t;

// Decode one request frame body (bare msgpack). Returns FMRB_OK if it is a
// well-formed request array [0, seq, cmd, payload?]; fills *out. An unknown
// command name still returns FMRB_OK with cmd=DBG_CMD_UNKNOWN and seq set, so
// the caller can reply with an error response. Returns FMRB_ERR_INVALID_PARAM
// only for structurally invalid input.
fmrb_err_t fmrb_dbg_proto_decode_req(const uint8_t *body, size_t len,
                                     fmrb_dbg_req_t *out);

// --- Response / event body builder ----------------------------------------
// Wraps a msgpack sbuffer+packer. After begin(), pack exactly one payload
// object (a map, or nil) using the exposed packer, then read the body.
typedef struct {
    msgpack_sbuffer sbuf;
    msgpack_packer  pk;
} fmrb_dbg_writer_t;

void fmrb_dbg_writer_init(fmrb_dbg_writer_t *w);
void fmrb_dbg_writer_destroy(fmrb_dbg_writer_t *w);

// Pack the [1, seq, err, header for a response. Caller then packs one payload.
void fmrb_dbg_resp_begin(fmrb_dbg_writer_t *w, uint16_t seq, int err);
// Pack the [2, 0, name, header for an event. Caller then packs one payload.
void fmrb_dbg_event_begin(fmrb_dbg_writer_t *w, const char *name);

// Bare msgpack body produced so far (points into the writer's sbuffer).
const uint8_t *fmrb_dbg_writer_body(const fmrb_dbg_writer_t *w, size_t *len);

// Convenience: response whose payload is {"ok": true} (or nil on err<0).
void fmrb_dbg_write_ok(fmrb_dbg_writer_t *w, uint16_t seq, int err);

// --- Small packing helpers (caller must pack the enclosing map header) -----
void fmrb_dbg_pack_key(msgpack_packer *pk, const char *key);
void fmrb_dbg_pack_str(msgpack_packer *pk, const char *s);
void fmrb_dbg_pack_kv_str(msgpack_packer *pk, const char *key, const char *val);
void fmrb_dbg_pack_kv_int(msgpack_packer *pk, const char *key, int64_t val);
void fmrb_dbg_pack_kv_bool(msgpack_packer *pk, const char *key, bool val);
