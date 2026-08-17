/**
 * The _fmrb module: the low-level half of the Python app framework.
 *
 * Mirrors what picoruby-fmrb-app's app.c and gfx.c give Ruby, at the same
 * granularity, so the Python FmrbApp / FmrbGfx in prelude/ can be a direct
 * transcription of their Ruby counterparts. Drawing calls are flat functions
 * taking a canvas id rather than methods on a canvas object; the class shape
 * belongs to the prelude.
 *
 * This file may only include py/ headers, fmrb_hid_msg.h (stdint alone) and
 * fmrb_mp_bridge.h: the qstr extractor preprocesses it with the host compiler,
 * where nothing from ESP-IDF resolves. Anything needing firmware headers goes
 * in fmrb_bridge.c.
 */

#include <string.h>

#include "py/builtin.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/reader.h"
#include "py/runtime.h"

#include "fmrb_hid_event.h"
#include "fmrb_mp_bridge.h"

// How long a single receive waits before the spin loop re-checks the stop
// flag. The kernel does send a stop message, but it gives up after 10ms if the
// queue is full, and then this poll is the only thing that ends the app.
#define FMRB_MP_SPIN_SLICE_MS (100)

// Ceiling for one read_file and one imported module. Both land in the GC heap
// (256KB, shared with everything the app allocates), so an unbounded read would
// take the app down with a memory error at an unrelated place later on.
#define FMRB_MP_READ_FILE_MAX (64 * 1024)

/* ---------------------------------------------------------------------------
 * msgpack
 *
 * APP_CONTROL payloads are msgpack maps built by the kernel and by other apps.
 * Only the subset those senders emit is decoded: maps, strings, integers,
 * booleans and nil. Anything else yields None for that value rather than
 * failing the whole message, so an unknown command still reaches on_control
 * with its cmd readable.
 * ------------------------------------------------------------------------ */

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
} mp_unpack_t;

static mp_obj_t unpack_value(mp_unpack_t *u);

static bool unpack_avail(mp_unpack_t *u, size_t n) {
    return (size_t)(u->end - u->p) >= n;
}

static uint32_t unpack_uint(mp_unpack_t *u, size_t width) {
    uint32_t v = 0;
    for (size_t i = 0; i < width; i++) {
        v = (v << 8) | *u->p++;
    }
    return v;
}

static mp_obj_t unpack_str(mp_unpack_t *u, size_t len) {
    if (!unpack_avail(u, len)) {
        return mp_const_none;
    }
    mp_obj_t s = mp_obj_new_str((const char *)u->p, len);
    u->p += len;
    return s;
}

static mp_obj_t unpack_bytes(mp_unpack_t *u, size_t len) {
    if (!unpack_avail(u, len)) {
        return mp_const_none;
    }
    mp_obj_t b = mp_obj_new_bytes(u->p, len);
    u->p += len;
    return b;
}

static mp_obj_t unpack_float(mp_unpack_t *u, size_t width) {
    // msgpack floats are big-endian IEEE-754. Rebuilt through a union rather
    // than a cast so the unaligned payload cannot trap on the 32-bit targets.
    if (!unpack_avail(u, width)) {
        return MP_OBJ_NULL;
    }
    if (width == 4) {
        union { uint32_t u; float f; } v;
        v.u = unpack_uint(u, 4);
        return mp_obj_new_float((mp_float_t)v.f);
    }
    union { uint64_t u; double d; } v;
    v.u = ((uint64_t)unpack_uint(u, 4) << 32);
    v.u |= unpack_uint(u, 4);
    return mp_obj_new_float((mp_float_t)v.d);
}

static mp_obj_t unpack_map(mp_unpack_t *u, size_t count) {
    mp_obj_t dict = mp_obj_new_dict(count);
    for (size_t i = 0; i < count; i++) {
        mp_obj_t key = unpack_value(u);
        mp_obj_t val = unpack_value(u);
        if (key == MP_OBJ_NULL) {
            break;
        }
        mp_obj_dict_store(dict, key, val);
    }
    return dict;
}

static mp_obj_t unpack_array(mp_unpack_t *u, size_t count) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < count; i++) {
        mp_obj_t val = unpack_value(u);
        if (val == MP_OBJ_NULL) {
            break;
        }
        mp_obj_list_append(list, val);
    }
    return list;
}

static mp_obj_t unpack_value(mp_unpack_t *u) {
    if (!unpack_avail(u, 1)) {
        return MP_OBJ_NULL;
    }
    uint8_t b = *u->p++;

    if (b <= 0x7F) {                       // positive fixint
        return MP_OBJ_NEW_SMALL_INT(b);
    }
    if (b >= 0xE0) {                       // negative fixint
        return MP_OBJ_NEW_SMALL_INT((int8_t)b);
    }
    if ((b & 0xF0) == 0x80) {              // fixmap
        return unpack_map(u, b & 0x0F);
    }
    if ((b & 0xF0) == 0x90) {              // fixarray
        return unpack_array(u, b & 0x0F);
    }
    if ((b & 0xE0) == 0xA0) {              // fixstr
        return unpack_str(u, b & 0x1F);
    }

    switch (b) {
        case 0xC0: return mp_const_none;
        case 0xC2: return mp_const_false;
        case 0xC3: return mp_const_true;
        case 0xCC: return unpack_avail(u, 1) ? MP_OBJ_NEW_SMALL_INT(unpack_uint(u, 1)) : MP_OBJ_NULL;
        case 0xCD: return unpack_avail(u, 2) ? MP_OBJ_NEW_SMALL_INT(unpack_uint(u, 2)) : MP_OBJ_NULL;
        case 0xCE: return unpack_avail(u, 4) ? mp_obj_new_int_from_uint(unpack_uint(u, 4)) : MP_OBJ_NULL;
        case 0xD0: return unpack_avail(u, 1) ? MP_OBJ_NEW_SMALL_INT((int8_t)unpack_uint(u, 1)) : MP_OBJ_NULL;
        case 0xD1: return unpack_avail(u, 2) ? MP_OBJ_NEW_SMALL_INT((int16_t)unpack_uint(u, 2)) : MP_OBJ_NULL;
        case 0xD2: return unpack_avail(u, 4) ? mp_obj_new_int((int32_t)unpack_uint(u, 4)) : MP_OBJ_NULL;
        case 0xC4: return unpack_avail(u, 1) ? unpack_bytes(u, unpack_uint(u, 1)) : MP_OBJ_NULL;
        case 0xC5: return unpack_avail(u, 2) ? unpack_bytes(u, unpack_uint(u, 2)) : MP_OBJ_NULL;
        case 0xCA: return unpack_float(u, 4);
        case 0xCB: return unpack_float(u, 8);
        case 0xD9: return unpack_avail(u, 1) ? unpack_str(u, unpack_uint(u, 1)) : MP_OBJ_NULL;
        case 0xDA: return unpack_avail(u, 2) ? unpack_str(u, unpack_uint(u, 2)) : MP_OBJ_NULL;
        case 0xDE: return unpack_avail(u, 2) ? unpack_map(u, unpack_uint(u, 2)) : MP_OBJ_NULL;
        case 0xDC: return unpack_avail(u, 2) ? unpack_array(u, unpack_uint(u, 2)) : MP_OBJ_NULL;
        default:   return mp_const_none;   // type we do not send; keep going
    }
}

/**
 * Encode a dict as a msgpack map. Only the value types the kernel accepts are
 * written; anything else raises, because a silently dropped field would show
 * up much later as a missing command argument.
 */
static size_t pack_value(uint8_t *out, size_t cap, mp_obj_t obj);

static size_t pack_str(uint8_t *out, size_t cap, const char *s, size_t len) {
    if (len < 32) {
        if (cap < 1 + len) {
            return 0;
        }
        out[0] = (uint8_t)(0xA0 | len);
        memcpy(out + 1, s, len);
        return 1 + len;
    }
    if (len < 256) {
        if (cap < 2 + len) {
            return 0;
        }
        out[0] = 0xD9;
        out[1] = (uint8_t)len;
        memcpy(out + 2, s, len);
        return 2 + len;
    }
    if (len < 65536) {
        if (cap < 3 + len) {
            return 0;
        }
        out[0] = 0xDA;
        out[1] = (uint8_t)(len >> 8);
        out[2] = (uint8_t)len;
        memcpy(out + 3, s, len);
        return 3 + len;
    }
    return 0;
}

static size_t pack_bin(uint8_t *out, size_t cap, const uint8_t *b, size_t len) {
    if (len < 256) {
        if (cap < 2 + len) {
            return 0;
        }
        out[0] = 0xC4;
        out[1] = (uint8_t)len;
        memcpy(out + 2, b, len);
        return 2 + len;
    }
    if (len < 65536) {
        if (cap < 3 + len) {
            return 0;
        }
        out[0] = 0xC5;
        out[1] = (uint8_t)(len >> 8);
        out[2] = (uint8_t)len;
        memcpy(out + 3, b, len);
        return 3 + len;
    }
    return 0;
}

/** fixmap / map16, fixarray / array16: one header, then the elements. */
static size_t pack_collection_header(uint8_t *out, size_t cap, size_t count, bool is_map) {
    if (count < 16) {
        if (cap < 1) {
            return 0;
        }
        out[0] = (uint8_t)((is_map ? 0x80 : 0x90) | count);
        return 1;
    }
    if (count < 65536) {
        if (cap < 3) {
            return 0;
        }
        out[0] = is_map ? 0xDE : 0xDC;
        out[1] = (uint8_t)(count >> 8);
        out[2] = (uint8_t)count;
        return 3;
    }
    return 0;
}

static size_t pack_int(uint8_t *out, size_t cap, mp_int_t v) {
    if (v >= 0 && v < 128) {
        if (cap < 1) {
            return 0;
        }
        out[0] = (uint8_t)v;
        return 1;
    }
    if (v < 0 && v >= -32) {
        if (cap < 1) {
            return 0;
        }
        out[0] = (uint8_t)v;
        return 1;
    }
    if (cap < 5) {
        return 0;
    }
    out[0] = 0xD2;
    out[1] = (uint8_t)((uint32_t)v >> 24);
    out[2] = (uint8_t)((uint32_t)v >> 16);
    out[3] = (uint8_t)((uint32_t)v >> 8);
    out[4] = (uint8_t)v;
    return 5;
}

static size_t pack_value(uint8_t *out, size_t cap, mp_obj_t obj) {
    if (obj == mp_const_none) {
        if (cap < 1) {
            return 0;
        }
        out[0] = 0xC0;
        return 1;
    }
    if (obj == mp_const_true || obj == mp_const_false) {
        if (cap < 1) {
            return 0;
        }
        out[0] = (obj == mp_const_true) ? 0xC3 : 0xC2;
        return 1;
    }
    if (mp_obj_is_str(obj)) {
        size_t len;
        const char *s = mp_obj_str_get_data(obj, &len);
        return pack_str(out, cap, s, len);
    }
    if (mp_obj_is_int(obj)) {
        return pack_int(out, cap, mp_obj_get_int(obj));
    }
    if (mp_obj_is_type(obj, &mp_type_bytes) || mp_obj_is_type(obj, &mp_type_bytearray)) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(obj, &bufinfo, MP_BUFFER_READ);
        return pack_bin(out, cap, (const uint8_t *)bufinfo.buf, bufinfo.len);
    }
    if (mp_obj_is_type(obj, &mp_type_dict)) {
        mp_map_t *map = mp_obj_dict_get_map(obj);
        size_t pos = pack_collection_header(out, cap, map->used, true);
        if (pos == 0) {
            return 0;
        }
        for (size_t i = 0; i < map->alloc; i++) {
            if (!mp_map_slot_is_filled(map, i)) {
                continue;
            }
            size_t n = pack_value(out + pos, cap - pos, map->table[i].key);
            if (n == 0) {
                return 0;
            }
            pos += n;
            n = pack_value(out + pos, cap - pos, map->table[i].value);
            if (n == 0) {
                return 0;
            }
            pos += n;
        }
        return pos;
    }
    if (mp_obj_is_type(obj, &mp_type_list) || mp_obj_is_type(obj, &mp_type_tuple)) {
        size_t count = 0;
        mp_obj_t *items = NULL;
        mp_obj_get_array(obj, &count, &items);
        size_t pos = pack_collection_header(out, cap, count, false);
        if (pos == 0) {
            return 0;
        }
        for (size_t i = 0; i < count; i++) {
            size_t n = pack_value(out + pos, cap - pos, items[i]);
            if (n == 0) {
                return 0;
            }
            pos += n;
        }
        return pos;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * App lifecycle
 * ------------------------------------------------------------------------ */

static void dict_store_str(mp_obj_t dict, qstr key, mp_obj_t value) {
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(key), value);
}

static mp_obj_t fmrb_init(void) {
    fmrb_mp_app_info_t info;
    if (fmrb_mp_bridge_app_init(&info) != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("app init failed"));
    }

    mp_obj_t d = mp_obj_new_dict(11);
    dict_store_str(d, MP_QSTR_name, mp_obj_new_str(info.name, strlen(info.name)));
    dict_store_str(d, MP_QSTR_canvas,
                   info.canvas_id < 0 ? mp_const_none : mp_obj_new_int(info.canvas_id));
    dict_store_str(d, MP_QSTR_bg_canvas,
                   info.bg_canvas_id < 0 ? mp_const_none : mp_obj_new_int(info.bg_canvas_id));
    dict_store_str(d, MP_QSTR_window_width, MP_OBJ_NEW_SMALL_INT(info.window_width));
    dict_store_str(d, MP_QSTR_window_height, MP_OBJ_NEW_SMALL_INT(info.window_height));
    dict_store_str(d, MP_QSTR_pos_x, MP_OBJ_NEW_SMALL_INT(info.pos_x));
    dict_store_str(d, MP_QSTR_pos_y, MP_OBJ_NEW_SMALL_INT(info.pos_y));
    dict_store_str(d, MP_QSTR_fullscreen, mp_obj_new_bool(info.fullscreen));
    dict_store_str(d, MP_QSTR_rounded_corners, mp_obj_new_bool(info.rounded_corners));
    dict_store_str(d, MP_QSTR_headless, mp_obj_new_bool(info.headless));
    dict_store_str(d, MP_QSTR_platform,
                   MP_OBJ_NEW_QSTR(info.is_esp32 ? MP_QSTR_esp32 : MP_QSTR_linux));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(fmrb_init_obj, fmrb_init);

static mp_obj_t fmrb_cleanup(void) {
    fmrb_mp_bridge_app_cleanup();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(fmrb_cleanup_obj, fmrb_cleanup);

static mp_obj_t fmrb_is_file_app(void) {
    return mp_obj_new_bool(fmrb_mp_bridge_is_file_app());
}
static MP_DEFINE_CONST_FUN_OBJ_0(fmrb_is_file_app_obj, fmrb_is_file_app);

static mp_obj_t fmrb_should_exit(void) {
    return mp_obj_new_bool(fmrb_mp_bridge_should_exit());
}
static MP_DEFINE_CONST_FUN_OBJ_0(fmrb_should_exit_obj, fmrb_should_exit);

static mp_obj_t fmrb_set_window_pos(mp_obj_t x_in, mp_obj_t y_in) {
    fmrb_mp_bridge_set_window_pos(mp_obj_get_int(x_in), mp_obj_get_int(y_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(fmrb_set_window_pos_obj, fmrb_set_window_pos);

static mp_obj_t fmrb_log(mp_obj_t level_in, mp_obj_t msg_in) {
    const char *level = mp_obj_str_get_str(level_in);
    fmrb_mp_bridge_log(level[0], mp_obj_str_get_str(msg_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(fmrb_log_obj, fmrb_log);

/**
 * Milliseconds since boot. The guest has no other clock: the time module lives
 * in extmod/, which the embed package does not carry, so timers and every
 * "how long ago" in an app are measured with this.
 */
static mp_obj_t fmrb_ticks_ms(void) {
    return mp_obj_new_int_from_uint(fmrb_mp_bridge_now_ms());
}
static MP_DEFINE_CONST_FUN_OBJ_0(fmrb_ticks_ms_obj, fmrb_ticks_ms);

/* ---------------------------------------------------------------------------
 * Files
 *
 * open() is not available to a guest (mpport.c raises), so these two are how a
 * Python app reads its own data. Both go through the firmware's file layer.
 * ------------------------------------------------------------------------ */

static mp_obj_t fmrb_file_size(mp_obj_t path_in) {
    uint32_t size = 0;
    if (fmrb_mp_bridge_file_size(mp_obj_str_get_str(path_in), &size) != 0) {
        return mp_const_none;
    }
    return mp_obj_new_int_from_uint(size);
}
static MP_DEFINE_CONST_FUN_OBJ_1(fmrb_file_size_obj, fmrb_file_size);

static mp_obj_t fmrb_read_file(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    uint32_t size = 0;
    if (fmrb_mp_bridge_file_size(path, &size) != 0) {
        return mp_const_none;
    }
    if (size > FMRB_MP_READ_FILE_MAX) {
        // Refuse rather than truncate: a half-read file is a bug that shows up
        // as corrupt data much later.
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("file too large to read"));
    }

    vstr_t vstr;
    vstr_init_len(&vstr, size);
    if (fmrb_mp_bridge_file_read(path, (uint8_t *)vstr.buf, size) != 0) {
        vstr_clear(&vstr);
        return mp_const_none;
    }
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_1(fmrb_read_file_obj, fmrb_read_file);

/**
 * Import hooks (MICROPY_ENABLE_EXTERNAL_IMPORT).
 *
 * The importer walks sys.path, asks mp_import_stat what is there, and hands the
 * winning path here to be read. sys.path itself is set by fmrb_mp.c to the app's
 * own directory and the shared library directory, so this pair never sees a
 * path the app was not allowed to reach.
 */
mp_import_stat_t mp_import_stat(const char *path) {
    switch (fmrb_mp_bridge_path_kind(path)) {
        case FMRB_MP_PATH_FILE: return MP_IMPORT_STAT_FILE;
        case FMRB_MP_PATH_DIR:  return MP_IMPORT_STAT_DIR;
        default:                return MP_IMPORT_STAT_NO_EXIST;
    }
}

void mp_reader_new_file(mp_reader_t *reader, qstr filename) {
    const char *path = qstr_str(filename);
    uint32_t size = 0;
    if (fmrb_mp_bridge_file_size(path, &size) != 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    if (size > FMRB_MP_READ_FILE_MAX) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("module too large to import"));
    }

    // Read the module in one go rather than byte by byte: over flash, the
    // stock reader's 20-byte buffer turns one import into hundreds of reads.
    // The buffer is GC memory and the lexer owns it until it closes the reader.
    byte *buf = m_new(byte, size ? size : 1);
    if (size > 0 && fmrb_mp_bridge_file_read(path, buf, size) != 0) {
        m_del(byte, buf, size);
        mp_raise_OSError(MP_EIO);
    }
    mp_reader_new_mem(reader, buf, size, size ? size : 1);
}

/**
 * The importer's entry point for a .py module.
 *
 * py/lexer.c only compiles its copy of this when one of the stock readers is
 * enabled, and neither is (see mpconfigport.h), so the port provides it. The
 * body is the same two lines: open a reader on the file, wrap it in a lexer.
 */
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    mp_reader_t reader;
    mp_reader_new_file(&reader, filename);
    return mp_lexer_new(filename, reader);
}

static mp_obj_t fmrb_send_message(mp_obj_t pid_in, mp_obj_t type_in, mp_obj_t data_in) {
    uint8_t buf[FMRB_MP_MSG_BUF_SIZE];
    size_t n = pack_value(buf, sizeof(buf), data_in);
    if (n == 0) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("message payload not encodable"));
    }
    bool ok = fmrb_mp_bridge_send(mp_obj_get_int(pid_in), mp_obj_get_int(type_in), buf,
                                  (uint32_t)n);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_3(fmrb_send_message_obj, fmrb_send_message);

/* ---------------------------------------------------------------------------
 * Event delivery
 * ------------------------------------------------------------------------ */

static mp_obj_t hid_event_to_dict(const uint8_t *data, size_t size) {
    fmrb_hid_event_t ev;
    if (!fmrb_hid_event_decode(data, (uint32_t)size, &ev)) {
        return mp_const_none;
    }

    mp_obj_t d = mp_obj_new_dict(6);
#define HID_PUT(key, value) dict_store_str(d, key, (value))
#define HID_PUT_INT(key, value) HID_PUT(key, MP_OBJ_NEW_SMALL_INT(value))
#define HID_PUT_TYPE(name) HID_PUT(MP_QSTR_type, MP_OBJ_NEW_QSTR(name))

    switch (ev.type) {
        case FMRB_HID_EVENT_KEY_DOWN:
        case FMRB_HID_EVENT_KEY_UP:
            HID_PUT_TYPE(ev.type == FMRB_HID_EVENT_KEY_DOWN ? MP_QSTR_key_down
                                                            : MP_QSTR_key_up);
            HID_PUT_INT(MP_QSTR_keycode, ev.keycode);
            HID_PUT_INT(MP_QSTR_scancode, ev.scancode);
            HID_PUT_INT(MP_QSTR_modifier, ev.modifier);
            HID_PUT_INT(MP_QSTR_character, (uint8_t)ev.character);
            return d;

        case FMRB_HID_EVENT_MOUSE_DOWN:
        case FMRB_HID_EVENT_MOUSE_UP:
            HID_PUT_TYPE(ev.type == FMRB_HID_EVENT_MOUSE_DOWN ? MP_QSTR_mouse_down
                                                              : MP_QSTR_mouse_up);
            HID_PUT_INT(MP_QSTR_button, ev.button);
            HID_PUT_INT(MP_QSTR_x, ev.x);
            HID_PUT_INT(MP_QSTR_y, ev.y);
            return d;

        case FMRB_HID_EVENT_MOUSE_MOVE:
            HID_PUT_TYPE(MP_QSTR_mouse_move);
            HID_PUT_INT(MP_QSTR_x, ev.x);
            HID_PUT_INT(MP_QSTR_y, ev.y);
            return d;

        // Gamepad events reach Ruby apps but had no Python mapping; the shared
        // decoder hands them over, so they are dicts now too.
        case FMRB_HID_EVENT_GAMEPAD_DOWN:
        case FMRB_HID_EVENT_GAMEPAD_UP:
            HID_PUT_TYPE(ev.type == FMRB_HID_EVENT_GAMEPAD_DOWN ? MP_QSTR_gamepad_down
                                                                : MP_QSTR_gamepad_up);
            HID_PUT_INT(MP_QSTR_gamepad_id, ev.gamepad_id);
            HID_PUT_INT(MP_QSTR_button, ev.button);
            return d;

        case FMRB_HID_EVENT_GAMEPAD_AXIS:
            HID_PUT_TYPE(MP_QSTR_gamepad_axis);
            HID_PUT_INT(MP_QSTR_gamepad_id, ev.gamepad_id);
            HID_PUT_INT(MP_QSTR_axis, ev.axis);
            HID_PUT_INT(MP_QSTR_value, ev.value);
            return d;

        case FMRB_HID_EVENT_KANA_MODE:
            HID_PUT_TYPE(MP_QSTR_kana_mode);
            HID_PUT_INT(MP_QSTR_mode, ev.kana_mode);
            return d;

        default:
            return mp_const_none;
    }

#undef HID_PUT_TYPE
#undef HID_PUT_INT
#undef HID_PUT
}

// Most arguments any of the calls below passes. mp_call_method_n_kw wants the
// bound method and self ahead of them in the same array, hence the +2 -- and a
// call that outgrows this array would write past its end and take the whole
// firmware down, so the count is checked rather than trusted.
#define FMRB_MP_CALL_MAX_ARGS (3)

static void call_if_present(mp_obj_t app, qstr method, size_t n_args, const mp_obj_t *args) {
    if (n_args > FMRB_MP_CALL_MAX_ARGS) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("callback arity too large"));
    }
    mp_obj_t dest[2];
    mp_load_method_maybe(app, method, dest);
    if (dest[0] == MP_OBJ_NULL) {
        return;
    }
    mp_obj_t call_args[2 + FMRB_MP_CALL_MAX_ARGS];
    call_args[0] = dest[0];
    call_args[1] = dest[1];
    for (size_t i = 0; i < n_args; i++) {
        call_args[2 + i] = args[i];
    }
    mp_call_method_n_kw(n_args, 0, call_args);
}

/** Dict lookup that answers None for a missing key instead of raising. */
static mp_obj_t dict_get_or_none(mp_obj_t dict, qstr key) {
    mp_map_elem_t *elem = mp_map_lookup(mp_obj_dict_get_map(dict), MP_OBJ_NEW_QSTR(key),
                                        MP_MAP_LOOKUP);
    return elem ? elem->value : mp_const_none;
}

static void dispatch_control(mp_obj_t app, const uint8_t *data, size_t size) {
    mp_unpack_t u = { .p = data, .end = data + size };
    mp_obj_t msg = unpack_value(&u);
    if (msg == MP_OBJ_NULL || !mp_obj_is_type(msg, &mp_type_dict)) {
        return;
    }

    mp_obj_t cmd = dict_get_or_none(msg, MP_QSTR_cmd);
    if (!mp_obj_is_str(cmd)) {
        return;
    }
    const char *c = mp_obj_str_get_str(cmd);

    if (strcmp(c, "resize") == 0) {
        // A runtime window <-> fullscreen switch carries the new mode; a plain
        // resize (corner drag) omits it and is windowed. Missing keys come back
        // as None rather than raising, because raising here would unwind
        // through the spin loop and take the app down over a stray message.
        mp_obj_t args[3] = {
            dict_get_or_none(msg, MP_QSTR_width),
            dict_get_or_none(msg, MP_QSTR_height),
            dict_get_or_none(msg, MP_QSTR_fullscreen),
        };
        if (args[0] != mp_const_none && args[1] != mp_const_none) {
            call_if_present(app, MP_QSTR__handle_resize, 3, args);
        }
        return;
    }
    if (strcmp(c, "suspend") == 0 || strcmp(c, "resume") == 0 ||
        strcmp(c, "stop") == 0 || strcmp(c, "clear_and_stop") == 0 ||
        strcmp(c, "quit_request") == 0) {
        call_if_present(app, MP_QSTR__handle_system_control, 1, &msg);
        return;
    }
    call_if_present(app, MP_QSTR_on_control, 1, &msg);
}

/**
 * Pump messages for up to timeout_ms, then return.
 *
 * The wait is sliced so the stop flag is re-checked even when no message
 * arrives: the kernel's stop message is sent with a 10ms timeout and is
 * dropped if the app's queue happens to be full, and then this poll is what
 * ends the app.
 */
static mp_obj_t fmrb_spin(mp_obj_t app, mp_obj_t timeout_in) {
    uint32_t timeout_ms = (uint32_t)mp_obj_get_int(timeout_in);
    uint32_t started = fmrb_mp_bridge_now_ms();
    uint8_t buf[FMRB_MP_MSG_BUF_SIZE];

    for (;;) {
        if (fmrb_mp_bridge_should_exit()) {
            return mp_const_none;
        }

        // Timers run here as well as between turns, so a timer still fires
        // while the app is parked waiting for a message. An effect that is
        // switched off by a timer (a note, a flash) would otherwise stay on
        // for the whole wait.
        call_if_present(app, MP_QSTR__run_timers, 0, NULL);

        uint32_t elapsed = fmrb_mp_bridge_now_ms() - started;
        if (elapsed >= timeout_ms) {
            return mp_const_none;
        }
        uint32_t remaining = timeout_ms - elapsed;
        uint32_t slice = remaining < FMRB_MP_SPIN_SLICE_MS ? remaining
                                                           : FMRB_MP_SPIN_SLICE_MS;

        int type = -1;
        int n = fmrb_mp_bridge_recv(buf, sizeof(buf), &type, slice);
        if (n < 0) {
            continue;
        }

        // Latch a stop before anything else looks at the message: dispatching
        // can raise, and the flag has to survive that.
        fmrb_mp_bridge_note_control(type, buf, (uint32_t)n);

        if (type == FMRB_MP_MSG_TYPE_HID_EVENT) {
            mp_obj_t ev = hid_event_to_dict(buf, (size_t)n);
            if (ev != mp_const_none) {
                call_if_present(app, MP_QSTR_on_event, 1, &ev);
            }
        } else if (type == FMRB_MP_MSG_TYPE_APP_CONTROL) {
            dispatch_control(app, buf, (size_t)n);
        }
    }
}
static MP_DEFINE_CONST_FUN_OBJ_2(fmrb_spin_obj, fmrb_spin);

/* ---------------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------------ */

static void check_gfx(int ret) {
    if (ret != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("graphics command failed"));
    }
}

#define ARG_INT(i) mp_obj_get_int(args[i])

static mp_obj_t gfx_clear(mp_obj_t cid, mp_obj_t color) {
    check_gfx(fmrb_mp_gfx_clear(mp_obj_get_int(cid), mp_obj_get_int(color)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(gfx_clear_obj, gfx_clear);

static mp_obj_t gfx_set_pixel(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_set_pixel(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_set_pixel_obj, 4, 4, gfx_set_pixel);

static mp_obj_t gfx_draw_line(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_draw_line(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                    ARG_INT(4), ARG_INT(5)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_line_obj, 6, 6, gfx_draw_line);

static mp_obj_t gfx_draw_rect(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_rect(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                               ARG_INT(4), ARG_INT(5), false));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_rect_obj, 6, 6, gfx_draw_rect);

static mp_obj_t gfx_fill_rect(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_rect(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                               ARG_INT(4), ARG_INT(5), true));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_fill_rect_obj, 6, 6, gfx_fill_rect);

static mp_obj_t gfx_draw_circle(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_circle(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                 ARG_INT(4), false));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_circle_obj, 5, 5, gfx_draw_circle);

static mp_obj_t gfx_fill_circle(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_circle(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                 ARG_INT(4), true));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_fill_circle_obj, 5, 5, gfx_fill_circle);

static mp_obj_t gfx_draw_round_rect(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_round_rect(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                     ARG_INT(4), ARG_INT(5), ARG_INT(6), false));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_round_rect_obj, 7, 7, gfx_draw_round_rect);

static mp_obj_t gfx_fill_round_rect(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_round_rect(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                     ARG_INT(4), ARG_INT(5), ARG_INT(6), true));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_fill_round_rect_obj, 7, 7, gfx_fill_round_rect);

static mp_obj_t gfx_draw_ellipse(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_ellipse(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                  ARG_INT(4), ARG_INT(5), false));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_ellipse_obj, 6, 6, gfx_draw_ellipse);

static mp_obj_t gfx_fill_ellipse(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_ellipse(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                  ARG_INT(4), ARG_INT(5), true));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_fill_ellipse_obj, 6, 6, gfx_fill_ellipse);

static mp_obj_t gfx_draw_triangle(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_triangle(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                   ARG_INT(4), ARG_INT(5), ARG_INT(6), ARG_INT(7), false));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_triangle_obj, 8, 8, gfx_draw_triangle);

static mp_obj_t gfx_fill_triangle(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_triangle(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                   ARG_INT(4), ARG_INT(5), ARG_INT(6), ARG_INT(7), true));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_fill_triangle_obj, 8, 8, gfx_fill_triangle);

static mp_obj_t gfx_draw_text(size_t n_args, const mp_obj_t *args) {
    bool has_bg = (n_args >= 6) && (args[5] != mp_const_none);
    // mixed = render ASCII with the built-in font and multi-byte runs with the
    // Japanese one, which is the only way to get both in a single line.
    bool mixed = (n_args >= 7) && mp_obj_is_true(args[6]);
    check_gfx(fmrb_mp_gfx_draw_text(ARG_INT(0), ARG_INT(1), ARG_INT(2),
                                    mp_obj_str_get_str(args[3]), ARG_INT(4),
                                    has_bg ? ARG_INT(5) : 0, has_bg, mixed));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_text_obj, 5, 7, gfx_draw_text);

static mp_obj_t gfx_set_font(size_t n_args, const mp_obj_t *args) {
    int size = (n_args >= 3 && args[2] != mp_const_none) ? ARG_INT(2) : 0;
    check_gfx(fmrb_mp_gfx_set_font(ARG_INT(0), ARG_INT(1), size));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_set_font_obj, 2, 3, gfx_set_font);

static mp_obj_t gfx_set_text_size(mp_obj_t cid, mp_obj_t size) {
    check_gfx(fmrb_mp_gfx_set_text_size(mp_obj_get_int(cid), mp_obj_get_int(size)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(gfx_set_text_size_obj, gfx_set_text_size);

/* ---------------------------------------------------------------------------
 * Images and sprites
 *
 * The pixels stay on the graphics side throughout: an app names a file, and
 * what comes back to Python is an id.
 * ------------------------------------------------------------------------ */

static mp_obj_t gfx_sync_file(mp_obj_t src, mp_obj_t dest) {
    return mp_obj_new_bool(fmrb_mp_gfx_sync_file(mp_obj_str_get_str(src),
                                                 mp_obj_str_get_str(dest)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(gfx_sync_file_obj, gfx_sync_file);

/** -> {"id":, "width":, "height":} or None when it could not be decoded. */
static mp_obj_t gfx_create_image(mp_obj_t cid, mp_obj_t path) {
    uint16_t id = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    if (fmrb_mp_gfx_create_image(mp_obj_get_int(cid), mp_obj_str_get_str(path), &id,
                                 &width, &height) != 0) {
        return mp_const_none;
    }
    mp_obj_t d = mp_obj_new_dict(3);
    dict_store_str(d, MP_QSTR_id, MP_OBJ_NEW_SMALL_INT(id));
    dict_store_str(d, MP_QSTR_width, MP_OBJ_NEW_SMALL_INT(width));
    dict_store_str(d, MP_QSTR_height, MP_OBJ_NEW_SMALL_INT(height));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_2(gfx_create_image_obj, gfx_create_image);

static mp_obj_t gfx_draw_image(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_draw_image(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                     ARG_INT(4), ARG_INT(5)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_image_obj, 6, 6, gfx_draw_image);

static mp_obj_t gfx_delete_image(mp_obj_t cid, mp_obj_t image_id) {
    check_gfx(fmrb_mp_gfx_delete_image(mp_obj_get_int(cid), mp_obj_get_int(image_id)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(gfx_delete_image_obj, gfx_delete_image);

static mp_obj_t gfx_draw_tile(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_draw_tile(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3),
                                    ARG_INT(4), ARG_INT(5), ARG_INT(6), ARG_INT(7)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_tile_obj, 8, 8, gfx_draw_tile);

static mp_obj_t gfx_create_sprite_image(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    int id = fmrb_mp_gfx_create_sprite_image(ARG_INT(0), ARG_INT(1), ARG_INT(2),
                                             ARG_INT(3), mp_obj_is_true(args[4]));
    if (id == 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to create sprite image"));
    }
    return MP_OBJ_NEW_SMALL_INT(id);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_create_sprite_image_obj, 5, 5,
                                           gfx_create_sprite_image);

static mp_obj_t gfx_load_sprite_image_bmp(mp_obj_t cid, mp_obj_t image_id, mp_obj_t path) {
    check_gfx(fmrb_mp_gfx_load_sprite_image_bmp(mp_obj_get_int(cid),
                                                mp_obj_get_int(image_id),
                                                mp_obj_str_get_str(path)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(gfx_load_sprite_image_bmp_obj, gfx_load_sprite_image_bmp);

static mp_obj_t gfx_delete_sprite_image(mp_obj_t cid, mp_obj_t image_id) {
    check_gfx(fmrb_mp_gfx_delete_sprite_image(mp_obj_get_int(cid),
                                              mp_obj_get_int(image_id)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(gfx_delete_sprite_image_obj, gfx_delete_sprite_image);

static mp_obj_t gfx_set_sprite_target(mp_obj_t cid, mp_obj_t image_id) {
    check_gfx(fmrb_mp_gfx_set_sprite_target(mp_obj_get_int(cid),
                                            mp_obj_get_int(image_id)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(gfx_set_sprite_target_obj, gfx_set_sprite_target);

/** ids is a list of sprite image ids, one per animation frame. */
static mp_obj_t gfx_create_sprite_instance(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    size_t count = 0;
    mp_obj_t *items = NULL;
    mp_obj_get_array(args[1], &count, &items);
    if (count == 0 || count > FMRB_MP_SPRITE_MAX_FRAMES) {
        mp_raise_ValueError(MP_ERROR_TEXT("frame count out of range"));
    }

    uint16_t image_ids[FMRB_MP_SPRITE_MAX_FRAMES];
    for (size_t i = 0; i < count; i++) {
        image_ids[i] = (uint16_t)mp_obj_get_int(items[i]);
    }

    int id = fmrb_mp_gfx_create_sprite_instance(ARG_INT(0), image_ids, (int)count,
                                                ARG_INT(2), ARG_INT(3), ARG_INT(4));
    if (id == 0) {
        mp_raise_msg(&mp_type_RuntimeError,
                     MP_ERROR_TEXT("failed to create sprite instance"));
    }
    return MP_OBJ_NEW_SMALL_INT(id);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_create_sprite_instance_obj, 5, 5,
                                           gfx_create_sprite_instance);

static mp_obj_t gfx_sprite_move(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    check_gfx(fmrb_mp_gfx_sprite_move(ARG_INT(0), ARG_INT(1), ARG_INT(2), ARG_INT(3)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_sprite_move_obj, 4, 4, gfx_sprite_move);

static mp_obj_t gfx_sprite_visible(mp_obj_t cid, mp_obj_t inst, mp_obj_t visible) {
    check_gfx(fmrb_mp_gfx_sprite_visible(mp_obj_get_int(cid), mp_obj_get_int(inst),
                                         mp_obj_is_true(visible)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(gfx_sprite_visible_obj, gfx_sprite_visible);

static mp_obj_t gfx_sprite_frame(mp_obj_t cid, mp_obj_t inst, mp_obj_t frame) {
    check_gfx(fmrb_mp_gfx_sprite_frame(mp_obj_get_int(cid), mp_obj_get_int(inst),
                                       mp_obj_get_int(frame)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(gfx_sprite_frame_obj, gfx_sprite_frame);

static mp_obj_t gfx_delete_sprite_instance(mp_obj_t cid, mp_obj_t inst) {
    check_gfx(fmrb_mp_gfx_delete_sprite_instance(mp_obj_get_int(cid),
                                                 mp_obj_get_int(inst)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(gfx_delete_sprite_instance_obj, gfx_delete_sprite_instance);

static mp_obj_t gfx_delete_all_sprites(mp_obj_t cid) {
    check_gfx(fmrb_mp_gfx_delete_all_sprites(mp_obj_get_int(cid)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gfx_delete_all_sprites_obj, gfx_delete_all_sprites);

/**
 * audio_note(on, ch, freq, vol, duty, sweep) -> bool
 *
 * The note_on / note_off half of FmrbAudio, without a dict. A song sends one
 * every few milliseconds, and on the device a collection stops the app for
 * 100-205 ms, which is audible -- so this path allocates nothing.
 */
static mp_obj_t fmrb_audio_note(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return mp_obj_new_bool(fmrb_mp_bridge_audio_note(mp_obj_is_true(args[0]),
                                                     ARG_INT(1), ARG_INT(2),
                                                     ARG_INT(3), ARG_INT(4),
                                                     ARG_INT(5)));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(fmrb_audio_note_obj, 6, 6, fmrb_audio_note);

static mp_obj_t fmrb_language(void) {
    const char *lang = fmrb_mp_bridge_language();
    return mp_obj_new_str(lang, strlen(lang));
}
static MP_DEFINE_CONST_FUN_OBJ_0(fmrb_language_obj, fmrb_language);

static mp_obj_t gfx_present(size_t n_args, const mp_obj_t *args) {
    bool explicit_pos = (n_args >= 3) && (args[1] != mp_const_none)
                        && (args[2] != mp_const_none);
    check_gfx(fmrb_mp_gfx_present(ARG_INT(0), explicit_pos ? ARG_INT(1) : 0,
                                  explicit_pos ? ARG_INT(2) : 0, explicit_pos));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_present_obj, 1, 3, gfx_present);

/* ------------------------------------------------------------------------ */

static const mp_rom_map_elem_t fmrb_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__fmrb) },

    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&fmrb_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_cleanup), MP_ROM_PTR(&fmrb_cleanup_obj) },
    { MP_ROM_QSTR(MP_QSTR_spin), MP_ROM_PTR(&fmrb_spin_obj) },
    { MP_ROM_QSTR(MP_QSTR_send_message), MP_ROM_PTR(&fmrb_send_message_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_window_pos), MP_ROM_PTR(&fmrb_set_window_pos_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_file_app), MP_ROM_PTR(&fmrb_is_file_app_obj) },
    { MP_ROM_QSTR(MP_QSTR_should_exit), MP_ROM_PTR(&fmrb_should_exit_obj) },
    { MP_ROM_QSTR(MP_QSTR_log), MP_ROM_PTR(&fmrb_log_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&fmrb_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_file), MP_ROM_PTR(&fmrb_read_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_file_size), MP_ROM_PTR(&fmrb_file_size_obj) },

    { MP_ROM_QSTR(MP_QSTR_gfx_clear), MP_ROM_PTR(&gfx_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_set_pixel), MP_ROM_PTR(&gfx_set_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_draw_line), MP_ROM_PTR(&gfx_draw_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_draw_rect), MP_ROM_PTR(&gfx_draw_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_fill_rect), MP_ROM_PTR(&gfx_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_draw_circle), MP_ROM_PTR(&gfx_draw_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_fill_circle), MP_ROM_PTR(&gfx_fill_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_draw_round_rect), MP_ROM_PTR(&gfx_draw_round_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_fill_round_rect), MP_ROM_PTR(&gfx_fill_round_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_draw_ellipse), MP_ROM_PTR(&gfx_draw_ellipse_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_fill_ellipse), MP_ROM_PTR(&gfx_fill_ellipse_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_draw_triangle), MP_ROM_PTR(&gfx_draw_triangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_fill_triangle), MP_ROM_PTR(&gfx_fill_triangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_draw_text), MP_ROM_PTR(&gfx_draw_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_present), MP_ROM_PTR(&gfx_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_set_font), MP_ROM_PTR(&gfx_set_font_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_set_text_size), MP_ROM_PTR(&gfx_set_text_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_language), MP_ROM_PTR(&fmrb_language_obj) },
    { MP_ROM_QSTR(MP_QSTR_audio_note), MP_ROM_PTR(&fmrb_audio_note_obj) },

    { MP_ROM_QSTR(MP_QSTR_gfx_sync_file), MP_ROM_PTR(&gfx_sync_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_create_image), MP_ROM_PTR(&gfx_create_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_draw_image), MP_ROM_PTR(&gfx_draw_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_delete_image), MP_ROM_PTR(&gfx_delete_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_draw_tile), MP_ROM_PTR(&gfx_draw_tile_obj) },

    { MP_ROM_QSTR(MP_QSTR_gfx_create_sprite_image),
      MP_ROM_PTR(&gfx_create_sprite_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_load_sprite_image_bmp),
      MP_ROM_PTR(&gfx_load_sprite_image_bmp_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_delete_sprite_image),
      MP_ROM_PTR(&gfx_delete_sprite_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_set_sprite_target), MP_ROM_PTR(&gfx_set_sprite_target_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_create_sprite_instance),
      MP_ROM_PTR(&gfx_create_sprite_instance_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_sprite_move), MP_ROM_PTR(&gfx_sprite_move_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_sprite_visible), MP_ROM_PTR(&gfx_sprite_visible_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_sprite_frame), MP_ROM_PTR(&gfx_sprite_frame_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_delete_sprite_instance),
      MP_ROM_PTR(&gfx_delete_sprite_instance_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_delete_all_sprites),
      MP_ROM_PTR(&gfx_delete_all_sprites_obj) },
};
static MP_DEFINE_CONST_DICT(fmrb_module_globals, fmrb_module_globals_table);

const mp_obj_module_t fmrb_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fmrb_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__fmrb, fmrb_user_cmodule);
