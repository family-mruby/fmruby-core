// Kanji conversion in the input path (doc/kanji_input/, K3). See host_kanji.h.

#include "host_kanji.h"

#include <stdio.h>
#include <string.h>

#include "fmrb_app.h"
#include "fmrb_attr.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_kernel.h"
#include "fmrb_keymap.h"
#include "fmrb_link_protocol.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "fmrb_transport.h"
#include "host_internal.h"
#include "host_task.h"
#include "kanji_lookup.h"

static const char *TAG = "kanji";

// Scancodes (USB HID usage IDs).
#define SC_ENTER     0x28
#define SC_ESCAPE    0x29
#define SC_BACKSPACE 0x2A
#define SC_SPACE     0x2C
#define SC_KP_ENTER  0x58
#define SC_HENKAN    0x8A   // JIS Henkan (International 4)
#define SC_MOD_FIRST 0xE0   // Left Control .. Right GUI
#define SC_MOD_LAST  0xE7

// The bottom line: misaki 8 through the hybrid text mode (ASCII in Font0,
// 6 px; everything else in misaki, 8 px), which every configuration has.
#define LINE_H        10
#define TEXT_Y        1
#define ASCII_W       6
#define WIDE_W        8
#define PAGE_MAX      9    // candidates a page holds (the number keys 1-9)
#define LOOKUP_WAIT_MS 3000

#define COLOR_BG      0x25   // dark blue-grey
#define COLOR_TEXT    0xFF
#define COLOR_DIM     0xB6
#define COLOR_NUM     0x9F
#define COLOR_SEL_BG  0xFF
#define COLOR_SEL_FG  0x00

#define CMD_MAX 64
#define CONSUMED_BYTES 32  // one bit per scancode

typedef enum {
    KJ_IDLE = 0,      // no reading
    KJ_COMPOSING,     // a reading, not converted
    KJ_WAITING,       // a lookup is outstanding
    KJ_CONVERTING,    // candidates shown
} kj_state_t;

typedef struct {
    kj_state_t state;
    char preedit[FMRB_KANJI_LOOKUP_READING_MAX];
    uint16_t len;
    bool no_cand;            // the last lookup found nothing

    // Conversion (valid in KJ_CONVERTING)
    uint16_t matched;        // bytes of the reading the candidates replace
    uint16_t count;
    uint16_t sel;

    // Lookup
    uint32_t wait_seq;
    fmrb_tick_t wait_since;
    bool lookups_off;        // the lookup task stopped answering

    uint8_t pid;             // the app the reading is typed into
    uint8_t consumed[CONSUMED_BYTES];

    // The line's canvas
    uint16_t canvas;
    int16_t canvas_w;
    int16_t canvas_h;        // the screen height it was made for
    bool creating;
    bool created;            // the create answered (deferred to the tick)
    uint16_t created_id;
    bool shown;
    bool dirty;
} kj_t;

// PSRAM: the internal RAM budget has no room for new statics.
FMRB_EXT_RAM_BSS_ATTR static kj_t s_kj;
FMRB_EXT_RAM_BSS_ATTR static gfx_cmd_t s_cmds[CMD_MAX];

static void redraw(void);
static uint16_t page_first(uint16_t sel);
static uint16_t page_end(uint16_t first);

// ---- small helpers ----------------------------------------------------------

static void consumed_set(uint8_t sc)   { s_kj.consumed[sc >> 3] |= (uint8_t)(1u << (sc & 7)); }
static bool consumed_take(uint8_t sc)
{
    uint8_t bit = (uint8_t)(1u << (sc & 7));
    if (!(s_kj.consumed[sc >> 3] & bit)) return false;
    s_kj.consumed[sc >> 3] &= (uint8_t)~bit;
    return true;
}

static int utf8_char_len(uint8_t lead)
{
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

// Width on the line of a UTF-8 run, in pixels.
static int text_width(const char *s, size_t len)
{
    int w = 0;
    for (size_t i = 0; i < len; ) {
        uint8_t c = (uint8_t)s[i];
        w += (c < 0x80) ? ASCII_W : WIDE_W;
        i += (size_t)utf8_char_len(c);
    }
    return w;
}

// The start of the longest tail of s that fits in max_w pixels.
static size_t tail_fitting(const char *s, size_t len, int max_w)
{
    size_t start = 0;
    while (start < len && text_width(s + start, len - start) > max_w) {
        start += (size_t)utf8_char_len((uint8_t)s[start]);
    }
    return start;
}

static uint16_t last_char_start(const char *s, uint16_t len)
{
    uint16_t i = len;
    while (i > 0) {
        i--;
        if (((uint8_t)s[i] & 0xC0) != 0x80) break;
    }
    return i;
}

static bool engaged_mode(void)
{
    return fmrb_kana_get_mode() != FMRB_KANA_OFF;
}

// ---- preedit and commits ----------------------------------------------------

static void clear_preedit(void)
{
    s_kj.len = 0;
    s_kj.state = KJ_IDLE;
    s_kj.no_cand = false;
}

static void drop_front(uint16_t n)
{
    if (n > s_kj.len) n = s_kj.len;
    memmove(s_kj.preedit, s_kj.preedit + n, s_kj.len - n);
    s_kj.len = (uint16_t)(s_kj.len - n);
    s_kj.state = s_kj.len ? KJ_COMPOSING : KJ_IDLE;
    s_kj.no_cand = false;
}

static void commit_kana(uint8_t pid)
{
    if (s_kj.len) {
        host_send_text_bytes(pid, s_kj.preedit, s_kj.len);
    }
    clear_preedit();
}

// The selected candidate replaces the matched part; the rest of the reading
// stays as the next reading (dict_format.md "longest match").
static void commit_candidate(uint8_t pid)
{
    size_t clen = 0;
    const char *c = fmrb_kanji_lookup_candidate(s_kj.sel, &clen);
    if (c && clen) {
        host_send_text_bytes(pid, c, clen);
        drop_front(s_kj.matched);
    } else {
        commit_kana(pid);
    }
}

// Everything as it shows: the candidate and the rest as kana.
static void commit_all(uint8_t pid)
{
    if (s_kj.state == KJ_CONVERTING) {
        commit_candidate(pid);
    }
    commit_kana(pid);
}

static void append(uint8_t pid, const uint8_t *bytes, uint8_t n)
{
    if (n == 0) return;
    if ((size_t)s_kj.len + n > sizeof(s_kj.preedit)) {
        // Longer than any reading: what is there goes as kana.
        commit_kana(pid);
    }
    memcpy(s_kj.preedit + s_kj.len, bytes, n);
    s_kj.len = (uint16_t)(s_kj.len + n);
    s_kj.state = KJ_COMPOSING;
    s_kj.no_cand = false;
}

static void start_lookup(void)
{
    uint32_t seq = s_kj.lookups_off ? 0 : fmrb_kanji_lookup_request(s_kj.preedit, s_kj.len);
    if (seq == 0) {
        s_kj.no_cand = true;
        return;
    }
    s_kj.state = KJ_WAITING;
    s_kj.wait_seq = seq;
    s_kj.wait_since = fmrb_task_get_tick_count();
}

// ---- the interface ----------------------------------------------------------

bool host_kanji_engaged(void)
{
    if (s_kj.len > 0) return true;
    return fmrb_kanji_lookup_ready() && engaged_mode();
}

bool host_kanji_waiting(void)
{
    return s_kj.state == KJ_WAITING;
}

host_kanji_action_t host_kanji_key(uint8_t pid, bool key_down, uint8_t sc,
                                   uint8_t mod, char ch,
                                   fmrb_kana_action_t action,
                                   const fmrb_kana_result_t *kana)
{
    if (!key_down) {
        // The release of a key whose press was taken here goes with it.
        if (consumed_take(sc)) return HOST_KANJI_CONSUME;
        return (action == FMRB_KANA_COMPOSE) ? HOST_KANJI_CLEAR_CHAR
             : (action == FMRB_KANA_CONSUME) ? HOST_KANJI_CONSUME
             : HOST_KANJI_PASS;
    }
    if (action == FMRB_KANA_CONSUME) {
        // A mode key (host_kanji_mode_changed follows) or a backspace that
        // erased the pending romaji.
        return HOST_KANJI_CONSUME;
    }
    if (s_kj.len && pid != s_kj.pid) {
        // The focus moved under an unfinished reading (tick normally catches
        // this first): it belongs to the app it was typed into.
        commit_all(s_kj.pid);
        redraw();
    }

    const bool ctrl_alt = (mod & (FMRB_KEYMAP_MOD_LCTRL | FMRB_KEYMAP_MOD_RCTRL |
                                  FMRB_KEYMAP_MOD_LALT | FMRB_KEYMAP_MOD_RALT)) != 0;

    if (action == FMRB_KANA_COMPOSE) {
        // Typing on accepts the candidate in front of it.
        if (s_kj.state == KJ_CONVERTING) commit_candidate(pid);
        append(pid, kana->out, kana->out_len);
        if (s_kj.len) s_kj.pid = pid;
        redraw();
        return HOST_KANJI_CLEAR_CHAR;
    }

    // The composer passed the key. It may still have finished a kana in front
    // of it (a pending "n" before a space).
    if (kana->out_len) {
        if (s_kj.state == KJ_CONVERTING) commit_candidate(pid);
        append(pid, kana->out, kana->out_len);
        s_kj.pid = pid;
    }
    if (s_kj.len == 0) {
        return HOST_KANJI_PASS;
    }

    if ((sc == SC_SPACE && !ctrl_alt) || sc == SC_HENKAN) {
        consumed_set(sc);
        if (s_kj.state == KJ_CONVERTING) {
            s_kj.sel = (uint16_t)((s_kj.sel + 1) % s_kj.count);
        } else {
            start_lookup();
        }
        redraw();
        return HOST_KANJI_CONSUME;
    }
    if ((sc == SC_ENTER || sc == SC_KP_ENTER) && !ctrl_alt) {
        consumed_set(sc);
        if (s_kj.state == KJ_CONVERTING) {
            commit_candidate(pid);
        } else {
            commit_kana(pid);
        }
        redraw();
        return HOST_KANJI_CONSUME;
    }
    if (sc == SC_ESCAPE) {
        consumed_set(sc);
        if (s_kj.state == KJ_CONVERTING) {
            s_kj.state = KJ_COMPOSING;
        } else {
            clear_preedit();
        }
        redraw();
        return HOST_KANJI_CONSUME;
    }
    if (sc == SC_BACKSPACE) {
        consumed_set(sc);
        s_kj.len = last_char_start(s_kj.preedit, s_kj.len);
        s_kj.state = s_kj.len ? KJ_COMPOSING : KJ_IDLE;
        s_kj.no_cand = false;
        redraw();
        return HOST_KANJI_CONSUME;
    }
    if (s_kj.state == KJ_CONVERTING && !ctrl_alt && ch >= '1' && ch <= '9') {
        consumed_set(sc);
        // The number picks from the page on the line.
        uint16_t first = page_first(s_kj.sel);
        uint16_t idx = (uint16_t)(first + (ch - '1'));
        if (idx < page_end(first)) {
            s_kj.sel = idx;
            commit_candidate(pid);
            redraw();
        }
        return HOST_KANJI_CONSUME;
    }
    if (sc == 0 || (sc >= SC_MOD_FIRST && sc <= SC_MOD_LAST)) {
        // A modifier on its own (Shift for the next letter) changes nothing.
        return HOST_KANJI_PASS;
    }
    // Any other key: what shows is committed, then the key does its own work.
    commit_all(pid);
    redraw();
    return HOST_KANJI_PASS;
}

void host_kanji_mode_changed(uint8_t pid)
{
    if (s_kj.len && !engaged_mode() && s_kj.state != KJ_WAITING) {
        commit_all(s_kj.len ? s_kj.pid : pid);
        redraw();
    }
}

void host_kanji_result(uint32_t seq)
{
    if (s_kj.state != KJ_WAITING || seq != s_kj.wait_seq) return;
    const fmrb_kanji_lookup_result_t *r = fmrb_kanji_lookup_result();
    if (r && r->seq == seq && r->found && r->count > 0 &&
        r->matched_bytes > 0 && r->matched_bytes <= s_kj.len) {
        s_kj.state = KJ_CONVERTING;
        s_kj.matched = r->matched_bytes;
        s_kj.count = r->count;
        s_kj.sel = 0;
        s_kj.no_cand = false;
    } else {
        s_kj.state = KJ_COMPOSING;
        s_kj.no_cand = true;
    }
    redraw();
}

// ---- the line ---------------------------------------------------------------

static int s_ncmd;

static gfx_cmd_t *next_cmd(void)
{
    if (s_ncmd >= CMD_MAX) return NULL;
    return &s_cmds[s_ncmd++];
}

// Text in pieces a command can carry, returning the x after it.
static int put_text(int x, const char *s, size_t len, uint8_t fg, uint8_t bg)
{
    size_t i = 0;
    while (i < len) {
        size_t n = 0;
        while (i + n < len) {
            size_t cl = (size_t)utf8_char_len((uint8_t)s[i + n]);
            if (n + cl > FMRB_GFX_MAX_TEXT_LEN - 8) break;
            n += cl;
        }
        if (n == 0) break;
        gfx_cmd_t *c = next_cmd();
        if (!c) break;
        fmrb_gfx_cmd_text_n(c, s_kj.canvas, (int16_t)x, TEXT_Y, s + i, n,
                            fg, bg, false, FMRB_FONT_SIZE_SMALL, 1);
        x += text_width(s + i, n);
        i += n;
    }
    return x;
}

static void put_rect(int x, int y, int w, int h, uint8_t color)
{
    gfx_cmd_t *c = next_cmd();
    if (c) fmrb_gfx_cmd_rect(c, s_kj.canvas, (int16_t)x, (int16_t)y,
                             (uint16_t)w, (uint16_t)h, color, true);
}

// Width of candidate i on the line: its number, itself, a gap.
static int cand_width(uint16_t i)
{
    size_t len = 0;
    const char *c = fmrb_kanji_lookup_candidate(i, &len);
    return ASCII_W + (c ? text_width(c, len) : 0) + ASCII_W;
}

// Room the candidates have, given the reading at the left and the counter at
// the right.
static int cand_room(void)
{
    int left = text_width(s_kj.preedit, s_kj.len);
    if (left > s_kj.canvas_w / 3) left = s_kj.canvas_w / 3;
    return s_kj.canvas_w - 2 - left - ASCII_W - 8 * ASCII_W - 2;
}

static uint16_t page_end(uint16_t first)
{
    int room = cand_room();
    int w = 0;
    uint16_t i = first;
    while (i < s_kj.count && i - first < PAGE_MAX) {
        int cw = cand_width(i);
        if (i > first && w + cw > room) break;
        w += cw;
        i++;
    }
    return i;
}

static uint16_t page_first(uint16_t sel)
{
    uint16_t first = 0;
    for (;;) {
        uint16_t end = page_end(first);
        if (sel < end || end <= first) return first;
        first = end;
    }
}

static void create_cb(uint8_t status, const uint8_t *payload, uint32_t len, void *user)
{
    (void)user;
    // Runs inside the transport's processing: only note it, the tick draws.
    s_kj.creating = false;
    if (status == 0 && payload && len >= sizeof(uint16_t)) {
        uint16_t id;
        memcpy(&id, payload, sizeof(id));
        s_kj.created_id = id;
        s_kj.created = true;
    } else {
        FMRB_LOGW(TAG, "conversion line: canvas not created (status %u)", status);
    }
}

// True when the canvas is there at the current screen size.
static bool ensure_canvas(int w, int h)
{
    if (s_kj.canvas && (s_kj.canvas_w != w || s_kj.canvas_h != h)) {
        // The screen changed size (the fullscreen high-resolution mode).
        gfx_cmd_t del;
        memset(&del, 0, sizeof(del));
        del.cmd_type = GFX_CMD_DELETE_CANVAS;
        del.canvas_id = s_kj.canvas;
        host_send_gfx_list(&del, 1);
        s_kj.canvas = 0;
        s_kj.shown = false;
    }
    if (s_kj.canvas) return true;
    if (s_kj.creating) return false;
    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_CREATE_CANVAS;
    cmd.params.create_canvas.width = w;
    cmd.params.create_canvas.height = LINE_H;
    cmd.params.create_canvas.z_order = FMRB_GFX_Z_SYSTEM_OVERLAY;
    cmd.params.create_canvas.use_transparent = 0;
    cmd.params.create_canvas.transparent_color = 0;
    if (host_send_gfx_async(&cmd, create_cb, NULL) == FMRB_OK) {
        s_kj.creating = true;
        s_kj.canvas_w = (int16_t)w;
        s_kj.canvas_h = (int16_t)h;
    }
    return false;
}

static void hide_line(void)
{
    if (!s_kj.canvas || !s_kj.shown) return;
    fmrb_link_graphics_set_canvas_visible_t v = { .canvas_id = s_kj.canvas, .visible = 0 };
    fmrb_transport_send(FMRB_LINK_TYPE_GRAPHICS, FMRB_LINK_GFX_SET_CANVAS_VISIBLE,
                        (const uint8_t *)&v, sizeof(v), FMRB_TRANSPORT_TIMEOUT_DEFAULT);
    s_kj.shown = false;
}

static void draw_line(void)
{
    if (s_kj.len == 0) {
        hide_line();
        s_kj.dirty = false;
        return;
    }
    int sw = 0, sh = 0;
    fmrb_host_get_screen_size(&sw, &sh);
    if (sw <= 0 || sh <= LINE_H) return;
    if (!ensure_canvas(sw, sh)) return;   // drawn once the canvas is there
    s_kj.dirty = false;

    s_ncmd = 0;
    gfx_cmd_t *c = next_cmd();
    fmrb_gfx_cmd_clear(c, s_kj.canvas, COLOR_BG);
    int x = 2;

    if (s_kj.state == KJ_CONVERTING) {
        // The reading (the matched part, then the rest dimmed), the page of
        // candidates, and where the selection is among all of them.
        int max_left = s_kj.canvas_w / 3;
        size_t start = tail_fitting(s_kj.preedit, s_kj.len, max_left);
        int x0 = x;
        if (start < s_kj.matched) {
            x = put_text(x, s_kj.preedit + start, s_kj.matched - start, COLOR_TEXT, COLOR_BG);
            x = put_text(x, s_kj.preedit + s_kj.matched, s_kj.len - s_kj.matched, COLOR_DIM, COLOR_BG);
        } else {
            x = put_text(x, s_kj.preedit + start, s_kj.len - start, COLOR_DIM, COLOR_BG);
        }
        put_rect(x0, LINE_H - 1, x - x0, 1, COLOR_TEXT);
        x += ASCII_W;
        uint16_t first = page_first(s_kj.sel);
        uint16_t end = page_end(first);
        for (uint16_t i = first; i < end; i++) {
            char num = (char)('1' + (i - first));
            x = put_text(x, &num, 1, COLOR_NUM, COLOR_BG);
            size_t len = 0;
            const char *cand = fmrb_kanji_lookup_candidate(i, &len);
            if (!cand) break;
            if (i == s_kj.sel) {
                x = put_text(x, cand, len, COLOR_SEL_FG, COLOR_SEL_BG);
            } else {
                x = put_text(x, cand, len, COLOR_TEXT, COLOR_BG);
            }
            x += ASCII_W;
        }
        char counter[16];
        int n = snprintf(counter, sizeof(counter), "%u/%u",
                         (unsigned)(s_kj.sel + 1), (unsigned)s_kj.count);
        put_text(s_kj.canvas_w - 2 - n * ASCII_W, counter, (size_t)n, COLOR_DIM, COLOR_BG);
    } else {
        // The reading; its tail when it is longer than the line.
        static const char k_none[] = "(\xE5\x80\x99\xE8\xA3\x9C\xE3\x81\xAA\xE3\x81\x97)"; // (候補なし)
        int room = s_kj.canvas_w - 4 - (s_kj.no_cand ? text_width(k_none, sizeof(k_none) - 1) + ASCII_W : 0);
        size_t start = tail_fitting(s_kj.preedit, s_kj.len, room);
        int x0 = x;
        x = put_text(x, s_kj.preedit + start, s_kj.len - start, COLOR_TEXT, COLOR_BG);
        put_rect(x0, LINE_H - 1, x - x0, 1, COLOR_TEXT);
        if (s_kj.no_cand) {
            put_text(x + ASCII_W, k_none, sizeof(k_none) - 1, COLOR_DIM, COLOR_BG);
        }
    }

    c = next_cmd();
    if (!c) {
        // Out of room: replace the last command with the present.
        c = &s_cmds[CMD_MAX - 1];
    }
    fmrb_gfx_cmd_present(c, s_kj.canvas, 0, (int16_t)(sh - LINE_H), 0xFF);
    host_send_gfx_list(s_cmds, s_ncmd);
    s_kj.shown = true;
}

static void redraw(void)
{
    s_kj.dirty = true;
    draw_line();
}

void host_kanji_tick(void)
{
    if (s_kj.created) {
        s_kj.created = false;
        s_kj.canvas = s_kj.created_id;
        FMRB_LOGI(TAG, "conversion line: canvas %u (%dx%d)", s_kj.canvas,
                  (int)s_kj.canvas_w, LINE_H);
    }
    if (s_kj.state == KJ_WAITING &&
        (fmrb_task_get_tick_count() - s_kj.wait_since) > FMRB_MS_TO_TICKS(LOOKUP_WAIT_MS)) {
        // The lookup task did not answer. Its buffers may still be in use, so
        // no further lookup is asked for; the reading can still be committed.
        FMRB_LOGE(TAG, "lookup %u not answered in %d ms: conversion off",
                  (unsigned)s_kj.wait_seq, LOOKUP_WAIT_MS);
        s_kj.lookups_off = true;
        s_kj.state = KJ_COMPOSING;
        s_kj.no_cand = true;
        s_kj.dirty = true;
    }
    if (s_kj.len && s_kj.state != KJ_WAITING) {
        // Kana input turned off from elsewhere (the clickable indicators, the
        // PC's IME), or the focus went to another app: the reading is
        // committed where it was typed, as it shows.
        bool focus_moved = false;
        fmrb_hid_routing_t routing;
        if (fmrb_kernel_get_hid_routing(&routing) == FMRB_OK &&
            routing.routing_enabled && routing.target_pid != s_kj.pid) {
            focus_moved = true;
        }
        if (!engaged_mode() || focus_moved) {
            fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(s_kj.pid);
            if (ctx && ctx->state == PROC_STATE_RUNNING) {
                commit_all(s_kj.pid);
            } else {
                clear_preedit();
            }
            s_kj.dirty = true;
        }
    }
    if (s_kj.dirty || (s_kj.len && s_kj.canvas && !s_kj.shown)) {
        draw_line();
    }
}
