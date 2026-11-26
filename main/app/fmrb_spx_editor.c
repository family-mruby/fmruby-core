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

/* The gem's canonical header (lib/gems is the source of truth; the copy under
   components/picoruby-esp32/... is made by `rake setup`). Included by path so
   the main component needs no extra include dir for one file. */
#include "../../lib/gems/picoruby-fmrb-editor-core/include/editor_core_api.h"

/* The current instance's :binstr length (runtime sp_ctx.c; the host C
   cannot include sp_ctx.h, so it is declared here). */
int *sp_ctx_ffi_bin_len(void);

/* Called on the editor's own task while the program is claimed
   (fmrb_app_spawner.c): once before the program starts, to reclaim documents
   a killed editor left, and once after it has ended, so their memory goes
   back. */
void fmrb_spx_ec_release_docs(void);

/* The documents the running Spinel editor holds, and which one it is working
   on. The editor cannot keep them in a class-level ivar: the next instance's
   entry resets that to nil, so nothing on the Ruby side would ever close them
   (every editor that ended used to leak its slot, and the sixth since boot
   found the table full: "Doc full" on every keystroke). The C side remembers
   them instead, and EditorCore asks for the current slot on every call
   (fmrb_editor_ffi.rb). One set is enough because only one Spinel editor runs
   at a time (fmrb_app_spinel_claim), so documents still recorded when an
   editor starts were left by one that was killed (the forced path skips the
   release), in whichever app slot it ran. In PSRAM: internal RAM is the
   scarce one. */
FMRB_EXT_RAM_BSS_ATTR static ec_docset_t s_docs;

int fmrb_spx_ec_slot(void)
{
    return ec_docset_slot(&s_docs);
}

void fmrb_spx_ec_release_docs(void)
{
    ec_docset_release(&s_docs);
}

int fmrb_spx_ec_doc_add(void)          { return ec_docset_add(&s_docs); }
int fmrb_spx_ec_doc_switch(int idx)    { return ec_docset_switch(&s_docs, idx); }
int fmrb_spx_ec_doc_close(void)        { return ec_docset_close(&s_docs); }
int fmrb_spx_ec_doc_index(void)        { return s_docs.cur; }

int fmrb_spx_ec_doc_count(void)
{
    if (ec_docset_slot(&s_docs) < 0) return 0;
    return s_docs.count;
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
int fmrb_spx_ec_load_range(int slot, const char *path, int offset, int length)
{
    return ec_load_range(slot, path, offset, length);
}
int fmrb_spx_ec_save_file(int slot, const char *path) { return ec_save_file(slot, path); }
int fmrb_spx_ec_replace_file(int slot, const char *path) { return ec_replace_file(slot, path); }
int fmrb_spx_ec_doc_version(int slot) { return ec_doc_version(slot); }

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
