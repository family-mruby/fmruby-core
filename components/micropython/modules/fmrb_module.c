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

#include "py/objstr.h"
#include "py/runtime.h"

#include "fmrb_hid_event.h"
#include "fmrb_mp_bridge.h"

// How long a single receive waits before the spin loop re-checks the stop
// flag. The kernel does send a stop message, but it gives up after 10ms if the
// queue is full, and then this poll is the only thing that ends the app.
#define FMRB_MP_SPIN_SLICE_MS (100)

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
    if (mp_obj_is_type(obj, &mp_type_dict)) {
        mp_map_t *map = mp_obj_dict_get_map(obj);
        if (map->used > 15 || cap < 1) {
            return 0;
        }
        out[0] = (uint8_t)(0x80 | map->used);
        size_t pos = 1;
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

        default:
            return mp_const_none;
    }

#undef HID_PUT_TYPE
#undef HID_PUT_INT
#undef HID_PUT
}

static void call_if_present(mp_obj_t app, qstr method, size_t n_args, const mp_obj_t *args) {
    mp_obj_t dest[2];
    mp_load_method_maybe(app, method, dest);
    if (dest[0] == MP_OBJ_NULL) {
        return;
    }
    mp_obj_t call_args[4];
    call_args[0] = dest[0];
    call_args[1] = dest[1];
    for (size_t i = 0; i < n_args; i++) {
        call_args[2 + i] = args[i];
    }
    mp_call_method_n_kw(n_args, 0, call_args);
}

static void dispatch_control(mp_obj_t app, const uint8_t *data, size_t size) {
    mp_unpack_t u = { .p = data, .end = data + size };
    mp_obj_t msg = unpack_value(&u);
    if (msg == MP_OBJ_NULL || !mp_obj_is_type(msg, &mp_type_dict)) {
        return;
    }

    mp_obj_t cmd = mp_obj_dict_get(msg, MP_OBJ_NEW_QSTR(MP_QSTR_cmd));
    if (!mp_obj_is_str(cmd)) {
        return;
    }
    const char *c = mp_obj_str_get_str(cmd);

    if (strcmp(c, "resize") == 0) {
        mp_obj_t w = mp_obj_dict_get(msg, MP_OBJ_NEW_QSTR(MP_QSTR_width));
        mp_obj_t h = mp_obj_dict_get(msg, MP_OBJ_NEW_QSTR(MP_QSTR_height));
        mp_obj_t args[2] = { w, h };
        call_if_present(app, MP_QSTR__handle_resize, 2, args);
        return;
    }
    if (strcmp(c, "suspend") == 0 || strcmp(c, "resume") == 0 ||
        strcmp(c, "stop") == 0 || strcmp(c, "clear_and_stop") == 0) {
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
    check_gfx(fmrb_mp_gfx_draw_text(ARG_INT(0), ARG_INT(1), ARG_INT(2),
                                    mp_obj_str_get_str(args[3]), ARG_INT(4),
                                    has_bg ? ARG_INT(5) : 0, has_bg));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gfx_draw_text_obj, 5, 6, gfx_draw_text);

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
};
static MP_DEFINE_CONST_DICT(fmrb_module_globals, fmrb_module_globals_table);

const mp_obj_module_t fmrb_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fmrb_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__fmrb, fmrb_user_cmodule);
