/*
 * Spinel FFI shim for EditorCore (picoruby-fmrb-editor-core).
 *
 * The document model itself is VM-independent C keyed by an int slot; this file
 * only adapts its (pointer, length) string returns to Spinel's :binstr
 * convention, which is "return the pointer, publish the length in
 * sp_ffi_bin_len". Compiled only when the editor VM is Spinel, so an mruby-only
 * build never links it (the mruby binding lives in the gem).
 */

#include <stddef.h>

#include "fmrb_attr.h"

/* The gem's canonical header (lib/add is the source of truth; the copy under
   components/picoruby-esp32/... is made by `rake setup`). Included by path so
   the main component needs no extra include dir for one file. */
#include "../../lib/add/picoruby-fmrb-editor-core/include/editor_core_api.h"

/* The current instance's :binstr length (runtime sp_ctx.c; the host C
   cannot include sp_ctx.h, so it is declared here). */
int *sp_ctx_ffi_bin_len(void);

/* Called on the editor's own task while the program is claimed
   (fmrb_app_spawner.c): once before the program starts, to reclaim a slot a
   killed editor left, and once after it has ended, so the document's memory
   goes back. */
void fmrb_spx_ec_release_slot(void);

/* The document slot the running Spinel editor holds, plus one (0 = none).
   The editor cannot keep it in a class-level ivar: the next instance's entry
   resets that to nil, so nothing on the Ruby side ever closed the slot (every
   editor that ended leaked one, and the sixth since boot found the table
   full: "Doc full" on every keystroke). The C side remembers it instead, and
   EditorCore asks for it on every call (fmrb_editor_ffi.rb). One variable is
   enough because only one Spinel editor runs at a time
   (fmrb_app_spinel_claim), so a slot still recorded when an editor starts
   was left by one that was killed (the forced path skips the release), in
   whichever app slot it ran. In PSRAM: internal RAM is the scarce one. */
FMRB_EXT_RAM_BSS_ATTR static int s_slot_plus1;

int fmrb_spx_ec_open_slot(void)
{
    /* The first call opens the slot and the rest get the same one back. */
    if (s_slot_plus1 > 0) {
        return s_slot_plus1 - 1;
    }
    int slot = ec_open_slot();
    if (slot >= 0) {
        s_slot_plus1 = slot + 1;
    }
    return slot;
}

void fmrb_spx_ec_release_slot(void)
{
    if (s_slot_plus1 > 0) {
        ec_close_slot(s_slot_plus1 - 1);
        s_slot_plus1 = 0;
    }
}

void fmrb_spx_ec_close_slot(int slot)
{
    /* Forget it too, or the next call would hand the closed slot back and
       the release at exit would close it again. */
    if (s_slot_plus1 == slot + 1) {
        s_slot_plus1 = 0;
    }
    ec_close_slot(slot);
}

int fmrb_spx_ec_reset(int slot)          { return ec_reset(slot); }
int fmrb_spx_ec_line_count(int slot)     { return ec_line_count(slot); }
int fmrb_spx_ec_line_length(int slot, int y) { return ec_line_length(slot, y); }
int fmrb_spx_ec_doc_bytesize(int slot)   { return ec_doc_bytesize(slot); }
int fmrb_spx_ec_mem_used(void)           { return ec_mem_used(); }
void fmrb_spx_ec_set_hl(int slot, int on) { ec_set_hl(slot, on); }

const char *fmrb_spx_ec_render_text(int slot, int y, int col0, int max_cols)
{
    int len = 0;
    const char *p = ec_render_text(slot, y, col0, max_cols, &len);
    *sp_ctx_ffi_bin_len() = len;
    return p;
}

const char *fmrb_spx_ec_render_hl(int slot, int y, int col0, int max_cols)
{
    int len = 0;
    const char *p = ec_render_hl(slot, y, col0, max_cols, &len);
    *sp_ctx_ffi_bin_len() = len;
    return p;
}

const char *fmrb_spx_ec_render_width(int slot, int y, int col0, int max_cols)
{
    int len = 0;
    const char *p = ec_render_width(slot, y, col0, max_cols, &len);
    *sp_ctx_ffi_bin_len() = len;
    return p;
}

int fmrb_spx_ec_wrap_count(int slot, int y, int view_cells)
{
    return ec_wrap_count(slot, y, view_cells);
}

int fmrb_spx_ec_wrap_start(int slot, int y, int view_cells, int seg)
{
    return ec_wrap_start(slot, y, view_cells, seg);
}

const char *fmrb_spx_ec_char_at(int slot, int y, int x)
{
    int len = 0;
    const char *p = ec_char_at(slot, y, x, &len);
    *sp_ctx_ffi_bin_len() = len;
    return p;
}

int fmrb_spx_ec_insert_text(int slot, int y, int x, const char *s, int len)
{
    return ec_insert_text(slot, y, x, s, len);
}

int fmrb_spx_ec_split_line(int slot, int y, int x)  { return ec_split_line(slot, y, x); }
int fmrb_spx_ec_join_line(int slot, int y)          { return ec_join_line(slot, y); }
int fmrb_spx_ec_delete_char(int slot, int y, int x) { return ec_delete_char(slot, y, x); }

int fmrb_spx_ec_delete_range(int slot, int sy, int sx, int ey, int ex)
{
    return ec_delete_range(slot, sy, sx, ey, ex);
}

const char *fmrb_spx_ec_insert_multiline(int slot, int y, int x, const char *s, int len)
{
    int n = 0;
    const char *p = ec_insert_multiline(slot, y, x, s, len, &n);
    *sp_ctx_ffi_bin_len() = n;
    return p;
}

int fmrb_spx_ec_load_file(int slot, const char *path) { return ec_load_file(slot, path); }
int fmrb_spx_ec_save_file(int slot, const char *path) { return ec_save_file(slot, path); }

const char *fmrb_spx_ec_find(int slot, const char *q, int qlen,
                             int from_y, int from_x, int after)
{
    int n = 0;
    const char *p = ec_find(slot, q, qlen, from_y, from_x, after, &n);
    *sp_ctx_ffi_bin_len() = n;
    return p;
}

int fmrb_spx_ec_copy_range(int slot, int sy, int sx, int ey, int ex)
{
    return ec_copy_range(slot, sy, sx, ey, ex);
}

const char *fmrb_spx_ec_paste_at(int slot, int y, int x)
{
    int n = 0;
    const char *p = ec_paste_at(slot, y, x, &n);
    *sp_ctx_ffi_bin_len() = n;
    return p;
}

int fmrb_spx_ec_clipboard_length(int slot) { return ec_clipboard_length(slot); }

/* Completion (editor_ti_bridge.c). Same shape as the rest: counts and
   positions as ints, strings as :binstr. */
int fmrb_spx_et_suggest(int slot, int y, int x)
{
    return et_suggest(slot, y, x);
}

const char *fmrb_spx_et_suggestion(int i, int field)
{
    int len = 0;
    const char *p = et_suggestion(i, field, &len);
    *sp_ctx_ffi_bin_len() = len;
    return p;
}

int fmrb_spx_et_max_source_bytes(void) { return et_max_source_bytes(); }

int fmrb_spx_et_hover(int slot, int y, int x)
{
    return et_hover(slot, y, x);
}

const char *fmrb_spx_et_hover_field(int field)
{
    int len = 0;
    const char *p = et_hover_field(field, &len);
    *sp_ctx_ffi_bin_len() = len;
    return p;
}

int fmrb_spx_et_hover_is_method(void) { return et_hover_is_method(); }

int fmrb_spx_et_call_context(int slot, int y, int x)
{
    return et_call_context(slot, y, x);
}

const char *fmrb_spx_et_call_field(int field)
{
    int len = 0;
    const char *p = et_call_field(field, &len);
    *sp_ctx_ffi_bin_len() = len;
    return p;
}

int fmrb_spx_et_call_argument_index(void) { return et_call_argument_index(); }

int fmrb_spx_et_diagnose(int slot) { return et_diagnose(slot); }

int fmrb_spx_et_diagnostic_pos(int i, int field)
{
    return et_diagnostic_pos(i, field);
}

const char *fmrb_spx_et_diagnostic_message(int i)
{
    int len = 0;
    const char *p = et_diagnostic_message(i, &len);
    *sp_ctx_ffi_bin_len() = len;
    return p;
}
