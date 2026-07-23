/* sp_marshal.c -- Marshal.dump / Marshal.load, split out of sp_runtime.h.

   A standalone translation unit. The read side uses the generic sp_json_* hooks
   (sp_gc.h); the construction side uses the sp_marshal_v vtable the generated TU
   installs at startup (sp_re_init). Result containers are built through vtable
   wrappers and rooted with SP_GC_ROOT (now shared via sp_gc.h) so a nested
   allocation during the parse can't free a partially-built value. CRuby 4.8 wire
   format; see sp_marshal.h. */
#include "sp_marshal.h"   /* sp_gc.h: sp_RbVal, hooks, SP_GC_ROOT, cls_ids */
#include "sp_alloc.h"     /* sp_str_alloc_raw, sp_str_set_len, sp_str_byte_len, sp_float_to_s */
#include <string.h>
#include <math.h>

/* Bignum codec lives in lib/sp_bigint.c. */
typedef struct sp_Bigint sp_Bigint;
int sp_bigint_sign(sp_Bigint *b);
size_t sp_bigint_byte_len(sp_Bigint *b);
size_t sp_bigint_to_le_bytes(sp_Bigint *b, unsigned char *out, size_t cap);
sp_Bigint *sp_bigint_from_le_bytes(int negative, const unsigned char *bytes, size_t n);

/* Inline sp_RbVal constructors (state-free; avoid pulling sp_runtime.h). Heap
   value types (array/hash/complex/rational) go through the vtable instead. */
static sp_RbVal mk_nil(void)        { sp_RbVal r; r.tag = SP_TAG_NIL;  r.cls_id = 0; r.v.i = 0; return r; }
static sp_RbVal mk_bool(mrb_bool b) { sp_RbVal r; r.tag = SP_TAG_BOOL; r.cls_id = 0; r.v.i = (b != 0); return r; }  /* full union word: bool equality reads v.i */
static sp_RbVal mk_int(mrb_int n)   { sp_RbVal r; r.tag = SP_TAG_INT;  r.cls_id = 0; r.v.i = n; return r; }
static sp_RbVal mk_float(mrb_float f){ sp_RbVal r; r.tag = SP_TAG_FLT;  r.cls_id = 0; r.v.f = f; return r; }
static sp_RbVal mk_str(const char *s){ sp_RbVal r; r.tag = SP_TAG_STR;  r.cls_id = 0; r.v.s = s; return r; }
static sp_RbVal mk_sym(sp_sym s)    { sp_RbVal r; r.tag = SP_TAG_SYM;  r.cls_id = 0; r.v.i = (mrb_int)s; return r; }
static sp_RbVal mk_bigint(void *p)  { sp_RbVal r; r.tag = SP_TAG_BIGINT; r.cls_id = 0; r.v.p = p; return r; }
static mrb_float poly_f(sp_RbVal v) { return v.tag == SP_TAG_FLT ? v.v.f : (mrb_float)v.v.i; }
static mrb_int   poly_i(sp_RbVal v) { return v.tag == SP_TAG_INT ? v.v.i : (mrb_int)v.v.f; }

static void mar_raise(const char *cls, const char *msg) {
  if (sp_marshal_v.raise) sp_marshal_v.raise(cls, msg);
}

/* ---- dump ---- */
static void sp_mar_raw(sp_mar_buf *b, const char *s, size_t n) { for (size_t i = 0; i < n; i++) sp_mar_b(b, (unsigned char)s[i]); }
void sp_mar_b(sp_mar_buf *b, unsigned char c) {
  if (b->len >= b->cap) { b->cap = b->cap ? b->cap * 2 : 64; b->p = (char *)realloc(b->p, b->cap); if (!b->p) sp_oom_die(); }
  b->p[b->len++] = (char)c;
}
/* CRuby's variable-length signed long encoding (Fixnums + every length/count). */
void sp_mar_long(sp_mar_buf *b, long n) {
  if (n == 0) { sp_mar_b(b, 0); return; }
  if (0 < n && n < 123) { sp_mar_b(b, (unsigned char)(n + 5)); return; }
  if (-124 < n && n < 0) { sp_mar_b(b, (unsigned char)((n - 5) & 0xff)); return; }
  unsigned char buf[1 + sizeof(long)];
  long v = n; int i;
  for (i = 1; i <= (int)sizeof(long); i++) {
    buf[i] = (unsigned char)(v & 0xff);
    v >>= 8;
    if (v == 0) { buf[0] = (unsigned char)i; break; }
    if (v == -1) { buf[0] = (unsigned char)(256 - i); break; }
  }
  sp_mar_raw(b, (char *)buf, (size_t)i + 1);
}
static void sp_mar_bytes(sp_mar_buf *b, const char *s, size_t n) { sp_mar_long(b, (long)n); sp_mar_raw(b, s, n); }
void sp_mar_sym(sp_mar_buf *b, const char *name) {
  for (int i = 0; i < b->nws; i++)
    if (!strcmp(b->wsyms[i], name)) { sp_mar_b(b, ';'); sp_mar_long(b, i); return; }
  if (b->nws >= b->cws) { b->cws = b->cws ? b->cws * 2 : 8; b->wsyms = (char **)realloc(b->wsyms, sizeof(char *) * (size_t)b->cws); if (!b->wsyms) sp_oom_die(); }
  b->wsyms[b->nws++] = strdup(name);
  sp_mar_b(b, ':'); sp_mar_bytes(b, name, strlen(name));
}
/* CRuby's Marshal float text: the shortest %.g that round-trips, exponent
   normalized ("1e+02" -> "1e2"); 0.0 is "0", 100.0 is "1e2", 0.1 is "0.1". */
static const char *sp_mar_float_str(double f, char *buf, size_t bufsz) {
  for (int prec = 1; prec <= 17; prec++) {
    snprintf(buf, bufsz, "%.*g", prec, f);
    if (strtod(buf, NULL) == f) break;
  }
  char *e = strchr(buf, 'e');
  if (e) {                       /* drop '+' and leading exponent zeros */
    char *d = e + 1; int neg = 0;
    if (*d == '+') d++;
    else if (*d == '-') { neg = 1; d++; }
    while (*d == '0' && d[1]) d++;
    char tail[32]; snprintf(tail, sizeof tail, "%s%s", neg ? "-" : "", d);
    snprintf(e + 1, bufsz - (size_t)(e + 1 - buf), "%s", tail);
  }
  return buf;
}
/* If `ptr` (object identity; NULL for an identity-less linkable like Float) was
   already written, emit `@id` and return 1; else reserve the next id, return 0. */
static int sp_mar_seen(sp_mar_buf *b, void *ptr) {
  if (ptr) for (int i = 0; i < b->nl; i++) if (b->lptr[i] == ptr) { sp_mar_b(b, '@'); sp_mar_long(b, b->lid[i]); return 1; }
  int id = b->link_next++;
  if (ptr) {
    if (b->nl >= b->cl) { b->cl = b->cl ? b->cl * 2 : 16; b->lptr = (void **)realloc(b->lptr, sizeof(void *) * (size_t)b->cl); b->lid = (int *)realloc(b->lid, sizeof(int) * (size_t)b->cl); }
    b->lptr[b->nl] = ptr; b->lid[b->nl] = id; b->nl++;
  }
  return 0;
}
static void sp_mar_w_hash(sp_mar_buf *b, sp_RbVal v) {
  sp_mar_b(b, '{');
  mrb_int n = sp_json_len_fn(v);
  sp_mar_long(b, n);
  for (mrb_int i = 0; i < n; i++) {
    sp_RbVal k, val; sp_json_hpair_fn(v, i, &k, &val);
    sp_mar_w(b, k); sp_mar_w(b, val);
  }
}
void sp_mar_w(sp_mar_buf *b, sp_RbVal v) {
  switch (v.tag) {
    case SP_TAG_NIL:  sp_mar_b(b, '0'); break;
    case SP_TAG_BOOL: sp_mar_b(b, v.v.b ? 'T' : 'F'); break;
    case SP_TAG_INT:  sp_mar_b(b, 'i'); sp_mar_long(b, (long)v.v.i); break;
    case SP_TAG_FLT: {
      if (sp_mar_seen(b, NULL)) break;
      sp_mar_b(b, 'f');
      mrb_float f = v.v.f; const char *fs; char fbuf[64];
      if (isnan(f)) fs = "nan";
      else if (isinf(f)) fs = f < 0 ? "-inf" : "inf";
      else fs = sp_mar_float_str((double)f, fbuf, sizeof fbuf);
      sp_mar_bytes(b, fs, strlen(fs));
      break;
    }
    case SP_TAG_SYM:  sp_mar_sym(b, sp_sym_name_fn ? sp_sym_name_fn((sp_sym)v.v.i) : ""); break;
    case SP_TAG_STR: {
      const char *s = v.v.s ? v.v.s : "";
      if (sp_mar_seen(b, (void *)(uintptr_t)s)) break;
      /* CRuby wraps a String in an encoding ivar: I "<bytes>" 1 :E T. */
      sp_mar_b(b, 'I'); sp_mar_b(b, '"'); sp_mar_bytes(b, s, sp_str_byte_len(s));
      sp_mar_long(b, 1); sp_mar_sym(b, "E"); sp_mar_b(b, 'T');
      break;
    }
    case SP_TAG_OBJ:
      if (v.cls_id == SP_BUILTIN_COMPLEX) {
        if (sp_mar_seen(b, v.v.p)) break;
        mrb_float *c = (mrb_float *)v.v.p;  /* sp_Complex = {re, im} */
        sp_mar_b(b, 'U'); sp_mar_sym(b, "Complex");
        sp_mar_seen(b, NULL);
        sp_mar_b(b, '['); sp_mar_long(b, 2);
        sp_mar_w(b, mk_float(c[0])); sp_mar_w(b, mk_float(c[1]));
      }
      else if (v.cls_id == SP_BUILTIN_RATIONAL) {
        if (sp_mar_seen(b, v.v.p)) break;
        mrb_int *q = (mrb_int *)v.v.p;  /* sp_Rational = {num, den} */
        sp_mar_b(b, 'U'); sp_mar_sym(b, "Rational");
        sp_mar_seen(b, NULL);
        sp_mar_b(b, '['); sp_mar_long(b, 2);
        sp_mar_w(b, mk_int(q[0])); sp_mar_w(b, mk_int(q[1]));
      }
      else {
        int kind = sp_json_kind_fn ? sp_json_kind_fn(v) : 0;
        if (kind == 1) {  /* array */
          if (sp_mar_seen(b, v.v.p)) break;
          sp_mar_b(b, '['); mrb_int n = sp_json_len_fn(v); sp_mar_long(b, n);
          for (mrb_int i = 0; i < n; i++) sp_mar_w(b, sp_json_aref_fn(v, i));
        }
        else if (kind == 2) {  /* hash */
          if (sp_mar_seen(b, v.v.p)) break;
          sp_mar_w_hash(b, v);
        }
        else if (v.cls_id >= 0) {  /* user object */
          if (sp_mar_seen(b, v.v.p)) break;
          if (!sp_marshal_v.obj_dump || !sp_marshal_v.obj_dump(b, v.cls_id, v.v.p))
            mar_raise("TypeError", "no marshal_dump is defined for this object");
        }
        else mar_raise("TypeError", "no marshal_dump is defined for this object");
      }
      break;
    case SP_TAG_BIGINT: {
      if (sp_mar_seen(b, v.v.p)) break;
      sp_Bigint *bg = (sp_Bigint *)v.v.p;
      size_t cap = sp_bigint_byte_len(bg) + 1;
      unsigned char *buf = (unsigned char *)calloc(cap + 1, 1);
      size_t nbytes = sp_bigint_to_le_bytes(bg, buf, cap);
      size_t nwords = (nbytes + 1) / 2; if (nwords == 0) nwords = 1;
      sp_mar_b(b, 'l');
      sp_mar_b(b, sp_bigint_sign(bg) < 0 ? '-' : '+');
      sp_mar_long(b, (long)nwords);
      for (size_t i = 0; i < nwords * 2; i++) sp_mar_b(b, i < nbytes ? buf[i] : 0);  /* LE, padded */
      free(buf);
      break;
    }
    default:
      mar_raise("TypeError", "unsupported value for Marshal.dump");
  }
}
const char *sp_marshal_dump(sp_RbVal v) {
  SP_GC_ROOT_RBVAL(v);   /* a Float component allocates via sp_float_to_s -> may GC */
  sp_mar_buf b; memset(&b, 0, sizeof b);
  sp_mar_b(&b, 4); sp_mar_b(&b, 8);  /* major.minor = 4.8 */
  sp_mar_w(&b, v);
  char *out = sp_str_alloc_raw(b.len + 1);
  memcpy(out, b.p, b.len); out[b.len] = 0;
  sp_str_set_len(out, b.len);
  for (int i = 0; i < b.nws; i++) free(b.wsyms[i]);
  free(b.wsyms);
  free(b.p); free(b.lptr); free(b.lid);
  return out;
}

/* ---- load ---- */
typedef struct {
  const char *s; size_t pos, len;
  char **syms; int nsym, csym;        /* symbol-link table (`;`) */
  sp_RbVal *objs; int nobj, cobj;     /* object-link table (`@`) */
} sp_mar_rd;
static unsigned char sp_mar_rb(sp_mar_rd *r) { return r->pos < r->len ? (unsigned char)r->s[r->pos++] : 0; }
static long sp_mar_rlong(sp_mar_rd *r) {
  int c = (signed char)sp_mar_rb(r);
  if (c == 0) return 0;
  if (c > 0) {
    if (4 < c && c < 128) return (long)c - 5;
    long x = 0;
    for (int i = 0; i < c && i < (int)sizeof(long); i++) x |= (long)sp_mar_rb(r) << (8 * i);
    return x;
  }
  if (-129 < c && c < -4) return (long)c + 5;
  c = -c;
  long x = -1;
  for (int i = 0; i < c && i < (int)sizeof(long); i++) {
    x &= ~((long)0xff << (8 * i));
    x |= (long)sp_mar_rb(r) << (8 * i);
  }
  return x;
}
static char *sp_mar_rstr(sp_mar_rd *r, long n) {
  if (n < 0) n = 0;
  char *out = sp_str_alloc_raw((size_t)n + 1);
  for (long i = 0; i < n; i++) out[i] = (char)sp_mar_rb(r);
  out[n] = 0;
  sp_str_set_len(out, (size_t)n);
  return out;
}
/* Reserve the next object-link id (nil placeholder so a self-ref resolves). */
static int sp_mar_reg(sp_mar_rd *r) {
  if (r->nobj >= r->cobj) { r->cobj = r->cobj ? r->cobj * 2 : 16; r->objs = (sp_RbVal *)realloc(r->objs, sizeof(sp_RbVal) * (size_t)r->cobj); }
  int id = r->nobj; r->objs[r->nobj++] = mk_nil(); return id;
}
static sp_sym mar_intern(const char *name) { return sp_marshal_v.sym_intern ? sp_marshal_v.sym_intern(name) : 0; }
static sp_RbVal sp_mar_r(sp_mar_rd *r) {
  unsigned char t = sp_mar_rb(r);
  switch (t) {
    case '0': return mk_nil();
    case 'T': return mk_bool(TRUE);
    case 'F': return mk_bool(FALSE);
    case 'i': return mk_int((mrb_int)sp_mar_rlong(r));
    case 'f': {
      int id = sp_mar_reg(r);
      long n = sp_mar_rlong(r);
      char *s = sp_mar_rstr(r, n); SP_GC_ROOT(s);
      mrb_float f;
      if (!strcmp(s, "inf")) f = (mrb_float)INFINITY;
      else if (!strcmp(s, "-inf")) f = -(mrb_float)INFINITY;
      else if (!strcmp(s, "nan")) f = (mrb_float)NAN;
      else f = strtod(s, NULL);
      sp_RbVal v = mk_float(f); r->objs[id] = v; return v;
    }
    case ':': {
      long n = sp_mar_rlong(r);
      char *name = sp_mar_rstr(r, n); SP_GC_ROOT(name);
      if (r->nsym >= r->csym) { r->csym = r->csym ? r->csym * 2 : 8; r->syms = (char **)realloc(r->syms, sizeof(char *) * (size_t)r->csym); }
      r->syms[r->nsym++] = strdup(name);
      return mk_sym(mar_intern(name));
    }
    case ';': {
      long idx = sp_mar_rlong(r);
      const char *name = (idx >= 0 && idx < r->nsym) ? r->syms[idx] : "";
      return mk_sym(mar_intern(name));
    }
    case '"': {
      int id = sp_mar_reg(r);
      sp_RbVal v = mk_str(sp_mar_rstr(r, sp_mar_rlong(r)));
      r->objs[id] = v; return v;
    }
    case 'I': {
      sp_RbVal inner = sp_mar_r(r);
      long nivar = sp_mar_rlong(r);
      for (long i = 0; i < nivar; i++) { sp_mar_r(r); sp_mar_r(r); }  /* encoding ivar: ignored */
      return inner;
    }
    case '@': {
      long id = sp_mar_rlong(r);
      return (id >= 0 && id < r->nobj) ? r->objs[id] : mk_nil();
    }
    case 'U': {
      int id = sp_mar_reg(r);
      sp_RbVal sym = sp_mar_r(r);
      sp_RbVal data = sp_mar_r(r); SP_GC_ROOT_RBVAL(data);
      const char *cn = (sym.tag == SP_TAG_SYM && sp_sym_name_fn) ? sp_sym_name_fn((sp_sym)sym.v.i) : "";
      sp_RbVal a0 = mk_nil(), a1 = mk_nil();
      if (data.tag == SP_TAG_OBJ) { a0 = sp_json_aref_fn(data, 0); a1 = sp_json_aref_fn(data, 1); }
      sp_RbVal v;
      if (!strcmp(cn, "Complex")) v = sp_marshal_v.box_complex(poly_f(a0), poly_f(a1));
      else if (!strcmp(cn, "Rational")) v = sp_marshal_v.box_rational(poly_i(a0), poly_i(a1));
      else { mar_raise("ArgumentError", "unsupported user class in Marshal.load"); v = mk_nil(); }
      r->objs[id] = v; return v;
    }
    case 'o': {
      int id = sp_mar_reg(r);
      sp_RbVal clssym = sp_mar_r(r);
      const char *cn = (clssym.tag == SP_TAG_SYM && sp_sym_name_fn) ? sp_sym_name_fn((sp_sym)clssym.v.i) : "";
      long n = sp_mar_rlong(r);
      sp_RbVal iv = sp_marshal_v.arr_new(); SP_GC_ROOT_RBVAL(iv);
      for (long i = 0; i < n; i++) {
        sp_marshal_v.arr_push(iv, sp_mar_r(r));   /* ivar symbol */
        sp_marshal_v.arr_push(iv, sp_mar_r(r));   /* ivar value  */
      }
      int ok = 0;
      sp_RbVal v = sp_marshal_v.obj_load(cn, iv, &ok);
      if (!ok) mar_raise("ArgumentError", "undefined class/module in Marshal.load");
      r->objs[id] = v; return v;
    }
    case 'l': {
      int id = sp_mar_reg(r);
      char sign = (char)sp_mar_rb(r);
      long nwords = sp_mar_rlong(r);
      size_t nbytes = (size_t)(nwords > 0 ? nwords : 0) * 2;
      unsigned char *buf = (unsigned char *)malloc(nbytes ? nbytes : 1);
      for (size_t i = 0; i < nbytes; i++) buf[i] = sp_mar_rb(r);
      sp_Bigint *bn = sp_bigint_from_le_bytes(sign == '-', buf, nbytes);
      free(buf);
      sp_RbVal v = mk_bigint(bn); r->objs[id] = v; return v;
    }
    case '[': {
      int id = sp_mar_reg(r);
      long n = sp_mar_rlong(r);
      sp_RbVal box = sp_marshal_v.arr_new(); SP_GC_ROOT_RBVAL(box);
      r->objs[id] = box;
      for (long i = 0; i < n; i++) sp_marshal_v.arr_push(box, sp_mar_r(r));
      return box;
    }
    case '{': {
      int id = sp_mar_reg(r);
      long n = sp_mar_rlong(r);
      sp_RbVal box = sp_marshal_v.hash_new(); SP_GC_ROOT_RBVAL(box);
      r->objs[id] = box;
      for (long i = 0; i < n; i++) { sp_RbVal k = sp_mar_r(r); sp_RbVal val = sp_mar_r(r); sp_marshal_v.hash_set(box, k, val); }
      return box;
    }
    default: mar_raise("ArgumentError", "unsupported type in Marshal.load"); return mk_nil();
  }
}
sp_RbVal sp_marshal_load(const char *s, mrb_int len) {
  sp_mar_rd r; memset(&r, 0, sizeof r);
  r.s = s ? s : ""; r.len = s ? (size_t)len : 0;
  if (r.len >= 2) r.pos = 2;  /* skip the 4.8 version header */
  sp_RbVal v = sp_mar_r(&r);
  for (int i = 0; i < r.nsym; i++) free(r.syms[i]);
  free(r.syms); free(r.objs);
  return v;
}
