/*
 * Spinel FFI shim for EditorCore (picoruby-fmrb-editor-core).
 *
 * The document model itself is VM-independent C keyed by an int slot; this file
 * only adapts its (pointer, length) string returns to Spinel's :binstr
 * convention, which is "return the pointer, publish the length in
 * sp_net_bin_len". Compiled only when the editor VM is Spinel, so an mruby-only
 * build never links it (the mruby binding lives in the gem).
 */

#include <stddef.h>

/* The gem's canonical header (lib/add is the source of truth; the copy under
   components/picoruby-esp32/... is made by `rake setup`). Included by path so
   the main component needs no extra include dir for one file. */
#include "../../lib/add/picoruby-fmrb-editor-core/include/editor_core_api.h"

/* Published by fmrb_spx_common.c; the runtime reads it after every :binstr
   call. */
extern int sp_net_bin_len;

int fmrb_spx_ec_open_slot(void)
{
    return ec_open_slot();
}

void fmrb_spx_ec_close_slot(int slot)
{
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
    sp_net_bin_len = len;
    return p;
}

const char *fmrb_spx_ec_render_hl(int slot, int y, int col0, int max_cols)
{
    int len = 0;
    const char *p = ec_render_hl(slot, y, col0, max_cols, &len);
    sp_net_bin_len = len;
    return p;
}

const char *fmrb_spx_ec_char_at(int slot, int y, int x)
{
    int len = 0;
    const char *p = ec_char_at(slot, y, x, &len);
    sp_net_bin_len = len;
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
    sp_net_bin_len = n;
    return p;
}

int fmrb_spx_ec_load_file(int slot, const char *path) { return ec_load_file(slot, path); }
int fmrb_spx_ec_save_file(int slot, const char *path) { return ec_save_file(slot, path); }

const char *fmrb_spx_ec_find(int slot, const char *q, int qlen,
                             int from_y, int from_x, int after)
{
    int n = 0;
    const char *p = ec_find(slot, q, qlen, from_y, from_x, after, &n);
    sp_net_bin_len = n;
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
    sp_net_bin_len = n;
    return p;
}

int fmrb_spx_ec_clipboard_length(int slot) { return ec_clipboard_length(slot); }
