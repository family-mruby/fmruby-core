/*
 * EditorCore: the mruby binding.
 *
 * All this layer does is map the calling mrb_state to its documents (a set of
 * slots and the current one) and turn the pure C API's (pointer, length)
 * results into mruby Strings. The document
 * model itself is in editor_core.c and knows nothing about mruby, which is what
 * lets the Spinel build of the editor use the same code through an FFI shim
 * (main/app/fmrb_spx_editor.c).
 */

#include <string.h>

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/class.h>

#include "fmrb_attr.h"

#include "../../include/editor_core_api.h"
#include "../../include/picoruby_fmrb_editor_core.h"

#define ECM_MAX_VMS 4

/* The documents each editor VM holds (ec_docset_t: up to EC_DOCSET_MAX, and
   which one it is working on). In PSRAM like the document table itself:
   internal RAM is the scarce one, and only the editors' own tasks read it. */
FMRB_EXT_RAM_BSS_ATTR static struct {
    mrb_state  *owner;
    ec_docset_t docs;
} g_vm_docs[ECM_MAX_VMS];

/* This VM's set, claimed on first use. NULL when every entry is taken. */
static ec_docset_t *docs_of(mrb_state *mrb)
{
    for (int i = 0; i < ECM_MAX_VMS; i++) {
        if (g_vm_docs[i].owner == mrb) return &g_vm_docs[i].docs;
    }
    for (int i = 0; i < ECM_MAX_VMS; i++) {
        if (g_vm_docs[i].owner == NULL) {
            g_vm_docs[i].owner = mrb;
            g_vm_docs[i].docs.count = 0;
            g_vm_docs[i].docs.cur = 0;
            return &g_vm_docs[i].docs;
        }
    }
    return NULL;
}

/* Slot of this VM's current document. A VM that cannot get one gets -1 and
   every call answers with the "no document" branch, which the editor reports
   as "Doc full" rather than failing silently. */
static int slot_of(mrb_state *mrb)
{
    ec_docset_t *d = docs_of(mrb);
    if (!d) return -1;
    int s = ec_docset_slot(d);
    return s < 0 ? -1 : s;
}

static void slot_release(mrb_state *mrb)
{
    for (int i = 0; i < ECM_MAX_VMS; i++) {
        if (g_vm_docs[i].owner == mrb) {
            ec_docset_release(&g_vm_docs[i].docs);
            g_vm_docs[i].owner = NULL;
            return;
        }
    }
}

static mrb_value str_of(mrb_state *mrb, const char *p, int len)
{
    if (!p || len <= 0) return mrb_str_new(mrb, "", 0);
    return mrb_str_new(mrb, p, (size_t)len);
}

/* ---- reading ----------------------------------------------------------- */

static mrb_value m_reset(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(ec_reset(slot_of(mrb)));
}

static mrb_value m_line_count(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(ec_line_count(slot_of(mrb)));
}

static mrb_value m_line_length(mrb_state *mrb, mrb_value self)
{
    mrb_int y;
    mrb_get_args(mrb, "i", &y);
    return mrb_fixnum_value(ec_line_length(slot_of(mrb), (int)y));
}

static mrb_value m_render_text(mrb_state *mrb, mrb_value self)
{
    mrb_int y, col0, max_cols;
    mrb_get_args(mrb, "iii", &y, &col0, &max_cols);
    int len = 0;
    const char *p = ec_render_text(slot_of(mrb), (int)y, (int)col0, (int)max_cols, &len);
    return str_of(mrb, p, len);
}

static mrb_value m_render_hl(mrb_state *mrb, mrb_value self)
{
    mrb_int y, col0, max_cols;
    mrb_get_args(mrb, "iii", &y, &col0, &max_cols);
    int len = 0;
    const char *p = ec_render_hl(slot_of(mrb), (int)y, (int)col0, (int)max_cols, &len);
    return str_of(mrb, p, len);
}

static mrb_value m_render_width(mrb_state *mrb, mrb_value self)
{
    mrb_int y, col0, max_cols;
    mrb_get_args(mrb, "iii", &y, &col0, &max_cols);
    int len = 0;
    const char *p = ec_render_width(slot_of(mrb), (int)y, (int)col0, (int)max_cols, &len);
    return str_of(mrb, p, len);
}

static mrb_value m_wrap_count(mrb_state *mrb, mrb_value self)
{
    mrb_int y, cells;
    mrb_get_args(mrb, "ii", &y, &cells);
    return mrb_fixnum_value(ec_wrap_count(slot_of(mrb), (int)y, (int)cells));
}

static mrb_value m_wrap_start(mrb_state *mrb, mrb_value self)
{
    mrb_int y, cells, seg;
    mrb_get_args(mrb, "iii", &y, &cells, &seg);
    return mrb_fixnum_value(ec_wrap_start(slot_of(mrb), (int)y, (int)cells, (int)seg));
}

static mrb_value m_char_at(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    int len = 0;
    const char *p = ec_char_at(slot_of(mrb), (int)y, (int)x, &len);
    return str_of(mrb, p, len);
}

static mrb_value m_hl_set(mrb_state *mrb, mrb_value self)
{
    mrb_bool on;
    mrb_get_args(mrb, "b", &on);
    ec_set_hl(slot_of(mrb), on ? 1 : 0);
    return mrb_nil_value();
}

static mrb_value m_doc_bytesize(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(ec_doc_bytesize(slot_of(mrb)));
}

static mrb_value m_mem_used(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(ec_mem_used());
}

/* ---- editing ----------------------------------------------------------- */

static mrb_value m_insert_text(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_value str;
    mrb_get_args(mrb, "iiS", &y, &x, &str);
    return mrb_fixnum_value(ec_insert_text(slot_of(mrb), (int)y, (int)x,
                                           RSTRING_PTR(str), (int)RSTRING_LEN(str)));
}

static mrb_value m_split_line(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    return mrb_fixnum_value(ec_split_line(slot_of(mrb), (int)y, (int)x));
}

static mrb_value m_join_line(mrb_state *mrb, mrb_value self)
{
    mrb_int y;
    mrb_get_args(mrb, "i", &y);
    return mrb_fixnum_value(ec_join_line(slot_of(mrb), (int)y));
}

static mrb_value m_delete_char(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    return mrb_fixnum_value(ec_delete_char(slot_of(mrb), (int)y, (int)x));
}

static mrb_value m_delete_range(mrb_state *mrb, mrb_value self)
{
    mrb_int sy, sx, ey, ex;
    mrb_get_args(mrb, "iiii", &sy, &sx, &ey, &ex);
    return mrb_fixnum_value(ec_delete_range(slot_of(mrb), (int)sy, (int)sx, (int)ey, (int)ex));
}

static mrb_value m_insert_multiline(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_value str;
    mrb_get_args(mrb, "iiS", &y, &x, &str);
    int len = 0;
    const char *p = ec_insert_multiline(slot_of(mrb), (int)y, (int)x,
                                        RSTRING_PTR(str), (int)RSTRING_LEN(str), &len);
    return str_of(mrb, p, len);
}

/* ---- file I/O ---------------------------------------------------------- */

static mrb_value m_load_file(mrb_state *mrb, mrb_value self)
{
    mrb_value path;
    mrb_get_args(mrb, "S", &path);
    return mrb_fixnum_value(ec_load_file(slot_of(mrb), RSTRING_CSTR(mrb, path)));
}

static mrb_value m_save_file(mrb_state *mrb, mrb_value self)
{
    mrb_value path;
    mrb_get_args(mrb, "S", &path);
    return mrb_fixnum_value(ec_save_file(slot_of(mrb), RSTRING_CSTR(mrb, path)));
}

/* ---- search / clipboard ------------------------------------------------ */

static mrb_value m_find(mrb_state *mrb, mrb_value self)
{
    mrb_value q;
    mrb_int from_y, from_x;
    mrb_bool after;
    mrb_get_args(mrb, "Siib", &q, &from_y, &from_x, &after);
    int len = 0;
    const char *p = ec_find(slot_of(mrb), RSTRING_PTR(q), (int)RSTRING_LEN(q),
                            (int)from_y, (int)from_x, after ? 1 : 0, &len);
    return str_of(mrb, p, len);
}

static mrb_value m_copy_range(mrb_state *mrb, mrb_value self)
{
    mrb_int sy, sx, ey, ex;
    mrb_get_args(mrb, "iiii", &sy, &sx, &ey, &ex);
    return mrb_fixnum_value(ec_copy_range(slot_of(mrb), (int)sy, (int)sx, (int)ey, (int)ex));
}

static mrb_value m_paste_at(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    int len = 0;
    const char *p = ec_paste_at(slot_of(mrb), (int)y, (int)x, &len);
    return str_of(mrb, p, len);
}

static mrb_value m_clipboard_length(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(ec_clipboard_length(slot_of(mrb)));
}

/* ---- documents --------------------------------------------------------- */

static mrb_value m_doc_add(mrb_state *mrb, mrb_value self)
{
    ec_docset_t *d = docs_of(mrb);
    return mrb_fixnum_value(d ? ec_docset_add(d) : EC_ERR_NOMEM);
}

static mrb_value m_doc_switch(mrb_state *mrb, mrb_value self)
{
    mrb_int idx;
    mrb_get_args(mrb, "i", &idx);
    ec_docset_t *d = docs_of(mrb);
    return mrb_fixnum_value(d ? ec_docset_switch(d, (int)idx) : EC_ERR_RANGE);
}

static mrb_value m_doc_close(mrb_state *mrb, mrb_value self)
{
    ec_docset_t *d = docs_of(mrb);
    return mrb_fixnum_value(d ? ec_docset_close(d) : 0);
}

static mrb_value m_doc_count(mrb_state *mrb, mrb_value self)
{
    ec_docset_t *d = docs_of(mrb);
    if (!d || ec_docset_slot(d) < 0) return mrb_fixnum_value(0);
    return mrb_fixnum_value(d->count);
}

static mrb_value m_doc_index(mrb_state *mrb, mrb_value self)
{
    ec_docset_t *d = docs_of(mrb);
    return mrb_fixnum_value(d ? d->cur : 0);
}

/* ---- completion -------------------------------------------------------- */

static mrb_value m_suggest(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    return mrb_fixnum_value(et_suggest(slot_of(mrb), (int)y, (int)x));
}

static mrb_value m_suggestion(mrb_state *mrb, mrb_value self)
{
    mrb_int i, field;
    mrb_get_args(mrb, "ii", &i, &field);
    int len = 0;
    const char *p = et_suggestion((int)i, (int)field, &len);
    return str_of(mrb, p, len);
}

static mrb_value m_max_source_bytes(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(et_max_source_bytes());
}

static mrb_value m_hover(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    return mrb_fixnum_value(et_hover(slot_of(mrb), (int)y, (int)x));
}

static mrb_value m_hover_field(mrb_state *mrb, mrb_value self)
{
    mrb_int field;
    mrb_get_args(mrb, "i", &field);
    int len = 0;
    const char *p = et_hover_field((int)field, &len);
    return str_of(mrb, p, len);
}

static mrb_value m_hover_is_method(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(et_hover_is_method() != 0);
}

static mrb_value m_call_context(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    return mrb_fixnum_value(et_call_context(slot_of(mrb), (int)y, (int)x));
}

static mrb_value m_call_field(mrb_state *mrb, mrb_value self)
{
    mrb_int field;
    mrb_get_args(mrb, "i", &field);
    int len = 0;
    const char *p = et_call_field((int)field, &len);
    return str_of(mrb, p, len);
}

static mrb_value m_call_argument_index(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(et_call_argument_index());
}

static mrb_value m_diagnose(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(et_diagnose(slot_of(mrb)));
}

static mrb_value m_diagnostic_pos(mrb_state *mrb, mrb_value self)
{
    mrb_int i, field;
    mrb_get_args(mrb, "ii", &i, &field);
    return mrb_fixnum_value(et_diagnostic_pos((int)i, (int)field));
}

static mrb_value m_diagnostic_message(mrb_state *mrb, mrb_value self)
{
    mrb_int i;
    mrb_get_args(mrb, "i", &i);
    int len = 0;
    const char *p = et_diagnostic_message((int)i, &len);
    return str_of(mrb, p, len);
}

/* ---- registration ------------------------------------------------------ */

void mrb_picoruby_fmrb_editor_core_init_impl(mrb_state *mrb)
{
    struct RClass *mod = mrb_define_module(mrb, "EditorCore");

    mrb_define_module_function(mrb, mod, "reset",        m_reset,        MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "line_count",   m_line_count,   MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "line_length",  m_line_length,  MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "render_text",  m_render_text,  MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, mod, "render_hl",    m_render_hl,    MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, mod, "render_width", m_render_width, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, mod, "wrap_count",  m_wrap_count,  MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "wrap_start",  m_wrap_start,  MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, mod, "char_at",      m_char_at,      MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "hl_enabled=",  m_hl_set,       MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "doc_bytesize", m_doc_bytesize, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "mem_used",     m_mem_used,     MRB_ARGS_NONE());

    mrb_define_module_function(mrb, mod, "insert_text",  m_insert_text,  MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, mod, "split_line",   m_split_line,   MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "join_line",    m_join_line,    MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "delete_char",  m_delete_char,  MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "delete_range", m_delete_range, MRB_ARGS_REQ(4));
    mrb_define_module_function(mrb, mod, "insert_multiline", m_insert_multiline, MRB_ARGS_REQ(3));

    mrb_define_module_function(mrb, mod, "load_file",    m_load_file,    MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "save_file",    m_save_file,    MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "find",         m_find,         MRB_ARGS_REQ(4));

    mrb_define_module_function(mrb, mod, "copy_range",   m_copy_range,   MRB_ARGS_REQ(4));
    mrb_define_module_function(mrb, mod, "paste_at",     m_paste_at,     MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "clipboard_length", m_clipboard_length, MRB_ARGS_NONE());

    /* Documents: the editor holds several and works on one at a time. Every
       call above goes to the current one, so none of them names a slot. */
    mrb_define_module_function(mrb, mod, "doc_add",      m_doc_add,      MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "doc_switch",   m_doc_switch,   MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "doc_close",    m_doc_close,    MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "doc_count",    m_doc_count,    MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "doc_index",    m_doc_index,    MRB_ARGS_NONE());

    /* Completion. suggest answers a count (negative on error), then each
       candidate is read field by field until the next suggest. */
    mrb_define_module_function(mrb, mod, "suggest",     m_suggest,     MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "suggestion",  m_suggestion,  MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "max_source_bytes", m_max_source_bytes, MRB_ARGS_NONE());

    /* Hover and diagnostics: same shape -- ask, then read the answer. */
    mrb_define_module_function(mrb, mod, "hover",          m_hover,          MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "hover_field",    m_hover_field,    MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "hover_method?",  m_hover_is_method, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "call_context",   m_call_context,   MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "call_field",     m_call_field,     MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "call_argument_index", m_call_argument_index, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "diagnose",       m_diagnose,       MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "diagnostic_pos", m_diagnostic_pos, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "diagnostic_message", m_diagnostic_message, MRB_ARGS_REQ(1));
}

void mrb_picoruby_fmrb_editor_core_final_impl(mrb_state *mrb)
{
    slot_release(mrb);
}
