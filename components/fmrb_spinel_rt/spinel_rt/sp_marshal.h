#ifndef SP_MARSHAL_H
#define SP_MARSHAL_H
/* Marshal.dump / Marshal.load -- public interface to lib/sp_marshal.c.

   The serializer is a standalone translation unit. The read side (length / array
   element / hash pair / kind / symbol name) reuses the generic sp_json_* hooks
   in sp_gc.h. The construction side (intern a symbol, build a result array/hash,
   box Complex/Rational, dispatch a user object, raise) needs types and state
   that live only in the generated TU, so the generated TU fills the sp_marshal_v
   vtable below at startup (see sp_re_init in codegen).

   Covers nil/true/false/Integer/Float/String/Symbol + Array + Hash + Bignum +
   Complex + Rational + plain user objects in the CRuby 4.8 wire format, with the
   object-link table so cyclic / shared references round-trip. */
#include "sp_gc.h"   /* sp_RbVal, sp_sym, mrb_int, mrb_float */

/* Dump buffer + wire-emit primitives. Public so the codegen-generated
   per-class object dumper (registered as sp_marshal_v.obj_dump) can write the
   `o` form through them. */
typedef struct sp_mar_buf_s {
  char *p; size_t len, cap;
  /* object-link table: pointer identity -> link id, plus the next id. */
  void **lptr; int *lid; int nl, cl; int link_next;
  /* symbol-link table: every symbol is written in full once; a repeat emits
     `;<index>` (indices count symbols only, separate from object links). */
  char **wsyms; int nws, cws;
} sp_mar_buf;
void sp_mar_b(sp_mar_buf *b, unsigned char c);
void sp_mar_sym(sp_mar_buf *b, const char *name);
void sp_mar_long(sp_mar_buf *b, long n);
void sp_mar_w(sp_mar_buf *b, sp_RbVal v);

/* Runtime vtable filled by the generated TU (sp_re_init). The read side uses
   the sp_json_* hooks in sp_gc.h instead. */
typedef struct {
  sp_sym  (*sym_intern)(const char *);
  sp_RbVal (*arr_new)(void);
  void     (*arr_push)(sp_RbVal arr, sp_RbVal v);
  sp_RbVal (*hash_new)(void);
  void     (*hash_set)(sp_RbVal h, sp_RbVal k, sp_RbVal v);
  sp_RbVal (*box_complex)(mrb_float re, mrb_float im);
  sp_RbVal (*box_rational)(mrb_int num, mrb_int den);
  int      (*obj_dump)(sp_mar_buf *b, int cls_id, void *p);   /* generated; writes `o` */
  sp_RbVal (*obj_load)(const char *clsname, sp_RbVal iv, int *ok); /* iv = boxed PolyArray */
  void     (*raise)(const char *cls, const char *msg);
} sp_marshal_vt;
extern sp_marshal_vt sp_marshal_v;

const char *sp_marshal_dump(sp_RbVal v);
sp_RbVal sp_marshal_load(const char *s, mrb_int len);
#endif /* SP_MARSHAL_H */
