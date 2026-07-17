// msgpack wire protocol for the remote debugger. See fmrb_debug_proto.h.
#include "fmrb_debug_proto.h"

#include <string.h>

// --- command name <-> enum -------------------------------------------------
static const struct { const char *name; fmrb_dbg_cmd_t cmd; } k_cmd_table[] = {
    { "version",     DBG_CMD_VERSION },
    { "ps",          DBG_CMD_PS },
    { "attach",      DBG_CMD_ATTACH },
    { "detach",      DBG_CMD_DETACH },
    { "bp_set",      DBG_CMD_BP_SET },
    { "bp_clear",    DBG_CMD_BP_CLEAR },
    { "pause",       DBG_CMD_PAUSE },
    { "continue",    DBG_CMD_CONTINUE },
    { "step_in",     DBG_CMD_STEP_IN },
    { "step_over",   DBG_CMD_STEP_OVER },
    { "step_out",    DBG_CMD_STEP_OUT },
    { "stack_trace", DBG_CMD_STACK_TRACE },
    { "frame_vars",  DBG_CMD_FRAME_VARS },
    { "expand",      DBG_CMD_EXPAND },
    { "log_read",    DBG_CMD_LOG_READ },
    { "kill",        DBG_CMD_KILL },
    { "stop",        DBG_CMD_STOP },
    { "suspend",     DBG_CMD_SUSPEND },
    { "resume",      DBG_CMD_RESUME },
    { "spawn",       DBG_CMD_SPAWN },
};

static fmrb_dbg_cmd_t cmd_from_str(const char *s, size_t len) {
    for (size_t i = 0; i < sizeof(k_cmd_table) / sizeof(k_cmd_table[0]); i++) {
        const char *n = k_cmd_table[i].name;
        if (strlen(n) == len && memcmp(n, s, len) == 0) {
            return k_cmd_table[i].cmd;
        }
    }
    return DBG_CMD_UNKNOWN;
}

// --- msgpack object readers ------------------------------------------------
static bool obj_is_int(const msgpack_object *o) {
    return o->type == MSGPACK_OBJECT_POSITIVE_INTEGER ||
           o->type == MSGPACK_OBJECT_NEGATIVE_INTEGER;
}

static int64_t obj_as_int(const msgpack_object *o) {
    if (o->type == MSGPACK_OBJECT_NEGATIVE_INTEGER) return o->via.i64;
    if (o->type == MSGPACK_OBJECT_POSITIVE_INTEGER) return (int64_t)o->via.u64;
    if (o->type == MSGPACK_OBJECT_BOOLEAN)          return o->via.boolean ? 1 : 0;
    return 0;
}

static void obj_copy_str(const msgpack_object *o, char *dst, size_t cap) {
    if (o->type != MSGPACK_OBJECT_STR || cap == 0) { if (cap) dst[0] = '\0'; return; }
    size_t n = o->via.str.size;
    if (n >= cap) n = cap - 1;
    memcpy(dst, o->via.str.ptr, n);
    dst[n] = '\0';
}

static bool key_is(const msgpack_object *k, const char *name) {
    return k->type == MSGPACK_OBJECT_STR &&
           strlen(name) == k->via.str.size &&
           memcmp(name, k->via.str.ptr, k->via.str.size) == 0;
}

static void decode_payload(const msgpack_object *map, fmrb_dbg_req_t *out) {
    if (map->type != MSGPACK_OBJECT_MAP) return;
    for (uint32_t i = 0; i < map->via.map.size; i++) {
        const msgpack_object *k = &map->via.map.ptr[i].key;
        const msgpack_object *v = &map->via.map.ptr[i].val;
        if (key_is(k, "pid") && obj_is_int(v)) {
            out->pid = (int)obj_as_int(v); out->have_pid = true;
        } else if (key_is(k, "line") && obj_is_int(v)) {
            out->line = (int)obj_as_int(v);
        } else if (key_is(k, "bp_id") && obj_is_int(v)) {
            out->bp_id = (int)obj_as_int(v);
        } else if (key_is(k, "frame") && obj_is_int(v)) {
            out->frame = (int)obj_as_int(v);
        } else if (key_is(k, "handle") && obj_is_int(v)) {
            out->handle = (int)obj_as_int(v);
        } else if (key_is(k, "max") && obj_is_int(v)) {
            out->max = (int)obj_as_int(v);
        } else if (key_is(k, "max_lines") && obj_is_int(v)) {
            out->max_lines = (int)obj_as_int(v);
        } else if (key_is(k, "pos") && obj_is_int(v)) {
            out->pos = (uint32_t)obj_as_int(v);
        } else if (key_is(k, "file")) {
            obj_copy_str(v, out->file, sizeof(out->file));
        } else if (key_is(k, "path")) {
            obj_copy_str(v, out->path, sizeof(out->path));
        }
    }
}

fmrb_err_t fmrb_dbg_proto_decode_req(const uint8_t *body, size_t len,
                                     fmrb_dbg_req_t *out) {
    memset(out, 0, sizeof(*out));

    msgpack_unpacked up;
    msgpack_unpacked_init(&up);
    size_t off = 0;
    msgpack_unpack_return ret =
        msgpack_unpack_next(&up, (const char *)body, len, &off);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&up);
        return FMRB_ERR_INVALID_PARAM;
    }

    const msgpack_object *root = &up.data;
    fmrb_err_t result = FMRB_OK;
    if (root->type != MSGPACK_OBJECT_ARRAY || root->via.array.size < 3) {
        result = FMRB_ERR_INVALID_PARAM;
        goto done;
    }
    const msgpack_object *arr = root->via.array.ptr;
    // arr[0] = type (must be request), arr[1] = seq, arr[2] = cmd
    if (!obj_is_int(&arr[0]) || obj_as_int(&arr[0]) != FMRB_DBG_MSG_REQUEST) {
        result = FMRB_ERR_INVALID_PARAM;
        goto done;
    }
    out->seq = obj_is_int(&arr[1]) ? (uint16_t)obj_as_int(&arr[1]) : 0;
    if (arr[2].type != MSGPACK_OBJECT_STR) {
        result = FMRB_ERR_INVALID_PARAM;
        goto done;
    }
    out->cmd = cmd_from_str(arr[2].via.str.ptr, arr[2].via.str.size);

    if (root->via.array.size >= 4) {
        decode_payload(&arr[3], out);
    }

done:
    msgpack_unpacked_destroy(&up);
    return result;
}

// --- writer ----------------------------------------------------------------
void fmrb_dbg_writer_init(fmrb_dbg_writer_t *w) {
    msgpack_sbuffer_init(&w->sbuf);
    msgpack_packer_init(&w->pk, &w->sbuf, msgpack_sbuffer_write);
}

void fmrb_dbg_writer_destroy(fmrb_dbg_writer_t *w) {
    msgpack_sbuffer_destroy(&w->sbuf);
}

void fmrb_dbg_resp_begin(fmrb_dbg_writer_t *w, uint16_t seq, int err) {
    msgpack_pack_array(&w->pk, 4);
    msgpack_pack_int(&w->pk, FMRB_DBG_MSG_RESPONSE);
    msgpack_pack_uint16(&w->pk, seq);
    msgpack_pack_int(&w->pk, err);
    // caller packs payload (map or nil) next
}

void fmrb_dbg_event_begin(fmrb_dbg_writer_t *w, const char *name) {
    msgpack_pack_array(&w->pk, 4);
    msgpack_pack_int(&w->pk, FMRB_DBG_MSG_EVENT);
    msgpack_pack_int(&w->pk, 0);
    fmrb_dbg_pack_str(&w->pk, name);
    // caller packs payload map next
}

const uint8_t *fmrb_dbg_writer_body(const fmrb_dbg_writer_t *w, size_t *len) {
    if (len) *len = w->sbuf.size;
    return (const uint8_t *)w->sbuf.data;
}

void fmrb_dbg_write_ok(fmrb_dbg_writer_t *w, uint16_t seq, int err) {
    fmrb_dbg_resp_begin(w, seq, err);
    if (err == FMRB_OK) {
        msgpack_pack_map(&w->pk, 1);
        fmrb_dbg_pack_kv_bool(&w->pk, "ok", true);
    } else {
        msgpack_pack_nil(&w->pk);
    }
}

// --- packing helpers -------------------------------------------------------
void fmrb_dbg_pack_str(msgpack_packer *pk, const char *s) {
    size_t n = s ? strlen(s) : 0;
    msgpack_pack_str(pk, n);
    if (n) msgpack_pack_str_body(pk, s, n);
}

void fmrb_dbg_pack_key(msgpack_packer *pk, const char *key) {
    fmrb_dbg_pack_str(pk, key);
}

void fmrb_dbg_pack_kv_str(msgpack_packer *pk, const char *key, const char *val) {
    fmrb_dbg_pack_str(pk, key);
    fmrb_dbg_pack_str(pk, val ? val : "");
}

void fmrb_dbg_pack_kv_int(msgpack_packer *pk, const char *key, int64_t val) {
    fmrb_dbg_pack_str(pk, key);
    msgpack_pack_int64(pk, val);
}

void fmrb_dbg_pack_kv_bool(msgpack_packer *pk, const char *key, bool val) {
    fmrb_dbg_pack_str(pk, key);
    if (val) msgpack_pack_true(pk); else msgpack_pack_false(pk);
}
