/*
 * A Lua app on the app shell (fmrb_app_shell.h, doc/app_boundary W3).
 *
 * The app exports global functions -- on_create, on_update (returns the ms
 * until the next call), on_event(ev) and, if it wants them, on_destroy,
 * on_suspend, on_resume, on_resize(w, h), on_quit_request -- and the shell
 * calls them. This file is the engine's side: it calls those functions in a
 * protected call, turns an event into a Lua table and passes the layout to
 * the drawer.
 */
#include "fmrb_lua.h"
#include "fmrb_app.h"
#include "fmrb_app_shell.h"
#include "fmrb_hid_decode.h"
#include "fmrb_log.h"
#include "fmrb_lua_gfx.h"
#include <stdio.h>
#include <string.h>

/* What the stop hook raises (fmrb_app.c lua_exit_check_hook). */
#define CLOSE_SENTINEL "__fmrb_close_requested__"

typedef struct {
    lua_State *L;
    int        chunk_ref;       /* the loaded program, run by HOOK_LOAD */
    char      *err;
    size_t     err_cap;
} lua_shell_t;

static const char *const HOOK_NAMES[FMRB_SHELL_HOOK_COUNT] = {
    [FMRB_SHELL_HOOK_LOAD]         = NULL,
    [FMRB_SHELL_HOOK_CREATE]       = "on_create",
    [FMRB_SHELL_HOOK_UPDATE]       = "on_update",
    [FMRB_SHELL_HOOK_EVENT]        = "on_event",
    [FMRB_SHELL_HOOK_DESTROY]      = "on_destroy",
    [FMRB_SHELL_HOOK_SUSPEND]      = "on_suspend",
    [FMRB_SHELL_HOOK_RESUME]       = "on_resume",
    [FMRB_SHELL_HOOK_RESIZE]       = "on_resize",
    [FMRB_SHELL_HOOK_QUIT_REQUEST] = "on_quit_request",
};

static int traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (msg && strstr(msg, CLOSE_SENTINEL)) {
        return 1;   /* not an error: keep it as it is */
    }
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

/* Call the function under nargs arguments on the stack; nresults kept. */
static int pcall(lua_shell_t *ls, int nargs, int nresults)
{
    lua_State *L = ls->L;
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    int r = lua_pcall(L, nargs, nresults, base);
    lua_remove(L, base);
    if (r == LUA_OK) {
        return FMRB_SHELL_OK;
    }
    const char *msg = lua_tostring(L, -1);
    int rc = FMRB_SHELL_RAISED;
    if (msg && strstr(msg, CLOSE_SENTINEL)) {
        rc = FMRB_SHELL_CLOSED;
    } else if (ls->err && ls->err_cap) {
        snprintf(ls->err, ls->err_cap, "%s", msg ? msg : "unknown error");
    }
    lua_pop(L, 1);
    return rc;
}

static bool push_hook(lua_shell_t *ls, fmrb_shell_hook_t hook)
{
    const char *name = HOOK_NAMES[hook];
    if (!name) return false;
    lua_getglobal(ls->L, name);
    if (lua_isfunction(ls->L, -1)) return true;
    lua_pop(ls->L, 1);
    return false;
}

static bool e_has_hook(void *ud, fmrb_shell_hook_t hook)
{
    lua_shell_t *ls = ud;
    if (!push_hook(ls, hook)) return false;
    lua_pop(ls->L, 1);
    return true;
}

static int e_call(void *ud, fmrb_shell_hook_t hook)
{
    lua_shell_t *ls = ud;
    if (hook == FMRB_SHELL_HOOK_LOAD) {
        lua_rawgeti(ls->L, LUA_REGISTRYINDEX, ls->chunk_ref);
        return pcall(ls, 0, 0);
    }
    if (!push_hook(ls, hook)) return FMRB_SHELL_OK;
    return pcall(ls, 0, 0);
}

static int e_call_update(void *ud, int *next_ms)
{
    lua_shell_t *ls = ud;
    *next_ms = -1;
    if (!push_hook(ls, FMRB_SHELL_HOOK_UPDATE)) return FMRB_SHELL_OK;
    int rc = pcall(ls, 0, 1);
    if (rc == FMRB_SHELL_OK) {
        if (lua_isnumber(ls->L, -1)) {
            lua_Integer ms = lua_tointeger(ls->L, -1);
            *next_ms = ms < 0 ? 0 : (ms > 0x7FFFFFFF ? 0x7FFFFFFF : (int)ms);
        }
        lua_pop(ls->L, 1);
    }
    return rc;
}

static void set_int(lua_State *L, const char *key, int v)
{
    lua_pushinteger(L, v);
    lua_setfield(L, -2, key);
}

/* The same keys and values as the event Hash of a Ruby app; type is a
   string with the Symbol's name. */
static int e_call_event(void *ud, const fmrb_shell_event_t *ev)
{
    lua_shell_t *ls = ud;
    lua_State *L = ls->L;
    const char *type = NULL;
    switch (ev->kind) {
    case FMRB_HID_EVENT_KEY_DOWN:     type = "key_down"; break;
    case FMRB_HID_EVENT_KEY_UP:       type = "key_up"; break;
    case FMRB_HID_EVENT_MOUSE_DOWN:   type = "mouse_down"; break;
    case FMRB_HID_EVENT_MOUSE_UP:     type = "mouse_up"; break;
    case FMRB_HID_EVENT_MOUSE_MOVE:   type = "mouse_move"; break;
    case FMRB_HID_EVENT_MOUSE_WHEEL:  type = "mouse_wheel"; break;
    case FMRB_HID_EVENT_GAMEPAD_DOWN: type = "gamepad_down"; break;
    case FMRB_HID_EVENT_GAMEPAD_UP:   type = "gamepad_up"; break;
    case FMRB_HID_EVENT_GAMEPAD_AXIS: type = "gamepad_axis"; break;
    case FMRB_HID_EVENT_KANA_MODE:    type = "kana_mode"; break;
    default: return FMRB_SHELL_OK;
    }
    if (!push_hook(ls, FMRB_SHELL_HOOK_EVENT)) return FMRB_SHELL_OK;
    lua_createtable(L, 0, 6);
    lua_pushstring(L, type);
    lua_setfield(L, -2, "type");
    switch (ev->kind) {
    case FMRB_HID_EVENT_KEY_DOWN:
    case FMRB_HID_EVENT_KEY_UP:
        set_int(L, "keycode", ev->keycode);
        set_int(L, "scancode", ev->scancode);
        set_int(L, "modifier", ev->modifier);
        set_int(L, "character", ev->character);
        break;
    case FMRB_HID_EVENT_MOUSE_DOWN:
    case FMRB_HID_EVENT_MOUSE_UP:
        set_int(L, "button", ev->button);
        /* fall through */
    case FMRB_HID_EVENT_MOUSE_MOVE:
        set_int(L, "x", ev->x);
        set_int(L, "y", ev->y);
        set_int(L, "wx", ev->wx);
        set_int(L, "wy", ev->wy);
        break;
    case FMRB_HID_EVENT_MOUSE_WHEEL:
        set_int(L, "delta", ev->delta);
        set_int(L, "x", ev->x);
        set_int(L, "y", ev->y);
        set_int(L, "wx", ev->wx);
        set_int(L, "wy", ev->wy);
        break;
    case FMRB_HID_EVENT_GAMEPAD_DOWN:
    case FMRB_HID_EVENT_GAMEPAD_UP:
        set_int(L, "gamepad_id", ev->gamepad_id);
        set_int(L, "button", ev->button);
        break;
    case FMRB_HID_EVENT_GAMEPAD_AXIS:
        set_int(L, "gamepad_id", ev->gamepad_id);
        set_int(L, "axis", ev->axis);
        set_int(L, "value", ev->value);
        break;
    case FMRB_HID_EVENT_KANA_MODE:
        set_int(L, "mode", ev->mode);
        break;
    default:
        break;
    }
    return pcall(ls, 1, 0);
}

static int e_call_resize(void *ud, int w, int h)
{
    lua_shell_t *ls = ud;
    if (!push_hook(ls, FMRB_SHELL_HOOK_RESIZE)) return FMRB_SHELL_OK;
    lua_pushinteger(ls->L, w);
    lua_pushinteger(ls->L, h);
    return pcall(ls, 2, 0);
}

static void e_layout(void *ud, const fmrb_shell_layout_t *layout)
{
    lua_shell_t *ls = ud;
    fmrb_lua_gfx_layout(ls->L, layout);
}

static const fmrb_shell_engine_t LUA_ENGINE = {
    .has_hook = e_has_hook,
    .call = e_call,
    .call_update = e_call_update,
    .call_event = e_call_event,
    .call_resize = e_call_resize,
    .layout = e_layout,
};

fmrb_err_t fmrb_lua_shell_run(fmrb_app_task_context_t *ctx, lua_State *L,
                              char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    lua_shell_t ls = {
        .L = L,
        .chunk_ref = luaL_ref(L, LUA_REGISTRYINDEX),   /* pops the chunk */
        .err = err,
        .err_cap = err_cap,
    };
    char why[160];
    fmrb_err_t ret = fmrb_app_shell_run(ctx, &LUA_ENGINE, &ls, why, sizeof(why));
    if (why[0] && err && err_cap) {
        snprintf(err, err_cap, "%s", why);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, ls.chunk_ref);
    return ret;
}
