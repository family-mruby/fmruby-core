#pragma once

#include <stddef.h>

/*
 * EditorCore, the VM-independent half.
 *
 * Documents are addressed by an int slot, not by an mruby state, so the same
 * implementation serves the mruby binding (ports/esp32/editor_core_mrb.c) and
 * the Spinel FFI shim (main/app/fmrb_spx_editor.c). Strings are returned as a
 * pointer plus an out length -- no allocator of the caller's is involved, and
 * both binding styles (mrb_str_new / Spinel :binstr + sp_net_bin_len) can wrap
 * that shape.
 *
 * Returned pointers stay valid until the next call that modifies that document.
 * Negative return values are errors: -1 range, -2 out of arena, -3 file I/O.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define EC_ERR_RANGE  (-1)
#define EC_ERR_NOMEM  (-2)
#define EC_ERR_IO     (-3)

/* Slots. Claim one per editor instance; close it when the VM goes away. */
int  ec_open_slot(void);
void ec_close_slot(int slot);

/* Longest run ec_render_width answers in one call. A 640px display is 106
   cells, so this covers a full row with room to spare. */
#define EC_WIDTH_MAX_COLS 256

/* Reading */
int  ec_reset(int slot);
int  ec_line_count(int slot);
int  ec_line_length(int slot, int y);
const char *ec_render_text(int slot, int y, int col0, int max_cols, int *out_len);
const char *ec_render_hl(int slot, int y, int col0, int max_cols, int *out_len);
/* Cell width (1 or 2) per character, same slice and shape as ec_render_hl.
   Bounded by EC_WIDTH_MAX_COLS; ask for no more than that at a time. */
const char *ec_render_width(int slot, int y, int col0, int max_cols, int *out_len);
/* Line wrapping at a given window width, in cells. ec_wrap_start(seg) with
   seg >= the count answers the line length, so segment [start(k), start(k+1))
   is the slice for screen row k. */
int  ec_wrap_count(int slot, int y, int view_cells);
int  ec_wrap_start(int slot, int y, int view_cells, int seg);
const char *ec_char_at(int slot, int y, int x, int *out_len);
void ec_set_hl(int slot, int on);
int  ec_doc_bytesize(int slot);
int  ec_mem_used(void);

/* Editing */
int  ec_insert_text(int slot, int y, int x, const char *s, int len);
int  ec_split_line(int slot, int y, int x);
int  ec_join_line(int slot, int y);
int  ec_delete_char(int slot, int y, int x);
int  ec_delete_range(int slot, int sy, int sx, int ey, int ex);
/* packed [y u32, x u32] */
const char *ec_insert_multiline(int slot, int y, int x, const char *s, int len,
                                int *out_len);

/* File I/O */
int  ec_load_file(int slot, const char *path);
int  ec_save_file(int slot, const char *path);

/* Search: packed [found u8, y u32, x u32] */
const char *ec_find(int slot, const char *q, int qlen, int from_y, int from_x,
                    int after, int *out_len);

/* Clipboard */
int  ec_copy_range(int slot, int sy, int sx, int ey, int ex);
const char *ec_paste_at(int slot, int y, int x, int *out_len);
int  ec_clipboard_length(int slot);

/* The document arena's allocator handle (POOL_ID_EDITOR_DOC), created on first
   use. For code that must allocate next to the document rather than in the
   app's own heap -- the type inference bridge below. Negative if the pool is
   unavailable. */
int  ec_doc_mem_handle(void);

/*
 * EditorTi: completion from the type inference engine (editor_ti_bridge.c).
 *
 * et_suggest runs one request for the document in `slot` with the cursor at
 * (y, x) -- y a line, x a CHARACTER index, as everywhere else here -- and
 * answers how many candidates it found. The candidates are then read one field
 * at a time with et_suggestion, and stay valid until the next et_suggest.
 */
#define ET_ERR_TOO_LARGE (-4)

#define ET_FIELD_LABEL   0   /* the name to insert */
#define ET_FIELD_DETAIL  1   /* its signature */
#define ET_FIELD_DOC     2   /* its doc comment, empty when it has none */

int  et_suggest(int slot, int y, int x);
const char *et_suggestion(int i, int field, int *out_len);
/* Documents above this many bytes are refused with ET_ERR_TOO_LARGE. */
int  et_max_source_bytes(void);

/*
 * Hover: what is under the cursor. et_hover answers 1 when it recognised
 * something, 0 when it did not. For a variable the interesting fields are the
 * name and the type; for a method call, the signature and its doc comment --
 * et_hover_is_method says which kind was found.
 */
#define ET_HOVER_NAME      0
#define ET_HOVER_TYPE      1
#define ET_HOVER_SIGNATURE 2
#define ET_HOVER_DOC       3

int  et_hover(int slot, int y, int x);
const char *et_hover_field(int field, int *out_len);
int  et_hover_is_method(void);

/*
 * Signature help: which call the cursor is inside, and which argument it has
 * reached. et_call_context answers 1 when the cursor is in a call whose method
 * is known, 0 when it is not; the signature and the current argument's name
 * and type are then read by field, and et_call_argument_index says which
 * parameter it is (0-based, -1 when the cursor is not in the argument list).
 */
#define ET_CALL_SIGNATURE 0
#define ET_CALL_ARG_NAME  1
#define ET_CALL_ARG_TYPE  2

int  et_call_context(int slot, int y, int x);
const char *et_call_field(int field, int *out_len);
int  et_call_argument_index(void);

/*
 * Diagnostics: type errors in the whole document. et_diagnose answers how many
 * were found; each one is then read by index. Positions are already converted
 * to the editor's coordinates (line, and column in characters).
 */
#define ET_DIAG_START_Y 0
#define ET_DIAG_START_X 1
#define ET_DIAG_END_Y   2
#define ET_DIAG_END_X   3

int  et_diagnose(int slot);
int  et_diagnostic_pos(int i, int field);
const char *et_diagnostic_message(int i, int *out_len);

#ifdef __cplusplus
}
#endif
