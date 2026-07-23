#ifndef SP_STR_H
#define SP_STR_H
/* sp_str.h -- cold String transforms compiled once in lib/sp_str.c.
 *
 * These are leaf `const char*` operations (case, strip, split-family,
 * partition, dump/undump, concat, repeat, ...) that depend only on the
 * shared string heap (sp_alloc.h) and the typed arrays (sp_array.h). The
 * The UTF-8 decode/advance/encode + length-cache lookup are inline here
 * (relocated from sp_runtime.h, codegen-neutral). The FNV hash cascade
 * (#282) and sp_str_eq stay inline in sp_runtime.h (optcarrot-sensitive).
 *
 * sp_sprintf / sp_raise_cls are provided by the generated TU and resolved
 * at the final link (same as lib/sp_core.c). */
#include "sp_array.h"   /* sp_StrArray, sp_IntArray + sp_alloc.h / sp_gc.h / sp_types.h */

const char *sp_sprintf(const char *fmt, ...);  /* defined in the generated TU */

/* ---- hot UTF-8 + length-cache inline core (relocated from sp_runtime.h;
   each generated TU still inlines these identically). Length-cache state
   (sp_str_lcache / SP_STR_LCACHE_*) lives in sp_alloc.h / sp_alloc.c. ---- */
static inline int sp_utf8_char_len(unsigned char c){if(c<0x80)return 1;if(c<0xC0)return 1;if(c<0xE0)return 2;if(c<0xF0)return 3;return 4;}
static inline int sp_utf8_advance(const char*p){int cn=sp_utf8_char_len((unsigned char)*p);int i=1;while(i<cn&&((unsigned char)p[i]&0xC0)==0x80)i++;return i;}
static inline int sp_utf8_decode(const char*p,uint32_t*out){unsigned char c=(unsigned char)p[0];if(c<0x80){*out=c;return 1;}if(c<0xC0){*out=c;return 1;}unsigned char c1=(unsigned char)p[1];if((c1&0xC0)!=0x80){*out=c;return 1;}if(c<0xE0){*out=((uint32_t)(c&0x1F)<<6)|(c1&0x3F);return 2;}unsigned char c2=(unsigned char)p[2];if((c2&0xC0)!=0x80){*out=c;return 1;}if(c<0xF0){*out=((uint32_t)(c&0x0F)<<12)|((uint32_t)(c1&0x3F)<<6)|(c2&0x3F);return 3;}unsigned char c3=(unsigned char)p[3];if((c3&0xC0)!=0x80){*out=c;return 1;}*out=((uint32_t)(c&0x07)<<18)|((uint32_t)(c1&0x3F)<<12)|((uint32_t)(c2&0x3F)<<6)|(c3&0x3F);return 4;}
static inline int sp_utf8_encode(uint32_t cp,char*out){if(cp<0x80){out[0]=(char)cp;return 1;}if(cp<0x800){out[0]=(char)(0xC0|(cp>>6));out[1]=(char)(0x80|(cp&0x3F));return 2;}if(cp<0x10000){out[0]=(char)(0xE0|(cp>>12));out[1]=(char)(0x80|((cp>>6)&0x3F));out[2]=(char)(0x80|(cp&0x3F));return 3;}out[0]=(char)(0xF0|(cp>>18));out[1]=(char)(0x80|((cp>>12)&0x3F));out[2]=(char)(0x80|((cp>>6)&0x3F));out[3]=(char)(0x80|(cp&0x3F));return 4;}
static inline unsigned sp_str_lcache_hash(const char *s) {
  uintptr_t k = (uintptr_t)s;
  return (unsigned)((k ^ (k >> 4) ^ (k >> 12)) & (SP_STR_LCACHE_SIZE - 1));
}
static inline int sp_str_cacheable(const char *s) {
  unsigned char m = ((const unsigned char *)s)[-1];
  return m == 0xfe || m == 0xfc || m == 0xff;
}
static inline void sp_str_split_push(sp_StrArray*a,const char*p,size_t n){
  char*r=sp_str_alloc_raw(n+1);
  memcpy(r,p,n);
  r[n]=0;
  sp_StrArray_push(a,r);
}

int sp_utf8_set_has(const uint32_t*cps,size_t n,uint32_t cp);
uint32_t sp_uc_toupper(uint32_t cp);
uint32_t sp_uc_tolower(uint32_t cp);
mrb_int sp_str_casecmp(const char*a,const char*b);
mrb_bool sp_str_valid_encoding(const char*s);
const char*sp_str_field(const char*s,const char*sep,mrb_int n);
mrb_int sp_str_field_count(const char*s,const char*sep);
const char*sp_str_concat(const char*a,const char*b);
const char*sp_str_concat3(const char*a,const char*b,const char*c);
const char*sp_str_concat4(const char*a,const char*b,const char*c,const char*d);
const char*sp_str_concat_arr(const char *const *parts,int n);
const char*sp_str_inspect(const char*s);
mrb_bool sp_sym_plain_name_p(const char *p, mrb_bool allow_suffix);
mrb_bool sp_sym_simple_p(const char *n);
const char *sp_sym_inspect_name(const char *name);
const char *sp_sym_inspect_key(const char *name);
const char*sp_str_upcase(const char*s);
const char*sp_str_downcase(const char*s);
const char*sp_str_swapcase(const char*s);
const char*sp_str_upcase_ascii(const char*s);
const char*sp_str_downcase_ascii(const char*s);
const char*sp_str_swapcase_ascii(const char*s);
const char*sp_str_capitalize_ascii(const char*s);
const char*sp_str_dump(const char*s);
const char*sp_str_delete_prefix(const char*s,const char*p);
const char*sp_str_substr(const char*s,mrb_int start,mrb_int len);
const char*sp_str_delete_suffix(const char*s,const char*p);
const char*sp_str_strip(const char*s);
const char*sp_str_chomp(const char*s);
const char *sp_str_chomp_sep(const char *s, const char *sep);
const char*sp_str_chop(const char*s);
mrb_bool sp_str_include(const char*s,const char*sub);
mrb_bool sp_str_start_with(const char*s,const char*p);
mrb_bool sp_str_end_with(const char*s,const char*suf);
sp_StrArray *sp_str_partition(const char *s, const char *sep);
sp_StrArray *sp_str_rpartition(const char *s, const char *sep);
sp_StrArray*sp_str_lines(const char*s);
sp_StrArray*sp_str_lines_sep(const char*s,const char*sep);
sp_StrArray*sp_str_lines_chomp(const char*s);
const char*sp_str_byteslice(const char*s,mrb_int start,mrb_int len);
const char*sp_str_byteslice1(const char*s,mrb_int i);
const char*sp_str_byteslice_range(const char*s,mrb_int lo,mrb_int hi,int excl,int lo_none,int hi_none);
const char*sp_str_bytesplice(const char*s,mrb_int start,mrb_int len,const char*val);
int sp_str_ascii_only(const char*s);
const char*sp_str_format_strarr(const char*fmt,sp_StrArray*a);
const char*sp_str_sub(const char*s,const char*pat,const char*rep);
const char*sp_str_capitalize(const char*s);
const char*sp_str_repeat(const char*s,mrb_int n);
sp_IntArray*sp_str_bytes(const char*s);
const char *sp_str_crypt(const char *s, const char *salt);
const char*sp_str_lstrip(const char*s);
const char*sp_str_rstrip(const char*s);
const char*sp_str_dup(const char*s);

/* ---- utf8-dependent cold transforms (lib/sp_str.c) ---- */
/* nil-receiver raise: a nullable string carries nil as NULL, and CRuby
   answers NoMethodError. The bare primitives below stay total over NULL
   (runtime internals pass legitimately-nil elements); the _m/_p variants
   carry Ruby receiver semantics at method call sites. */
SP_NORETURN SP_COLD void sp_nil_recv(const char *meth);
mrb_int sp_str_length_m(const char *s);
mrb_int sp_str_bytesize_m(const char *s);
mrb_bool sp_str_empty_p(const char *s);
const char *sp_str_plus(const char *a, const char *b);
mrb_int sp_str_count_chars(const char *s, size_t bl);
mrb_int sp_str_length(const char*s);
mrb_int sp_str_ord(const char*s);
size_t sp_utf8_byte_offset(const char*s,mrb_int char_idx);
uint32_t*sp_utf8_decode_all(const char*s,size_t*out_n);
uint32_t*sp_utf8_decode_charset(const char*s,size_t*out_n);
uint32_t*sp_utf8_decode_charset_n(const char*s,size_t bl,size_t*out_n);
void sp_str_split_into(sp_StrArray*a,const char*s,const char*sep);
const char*sp_str_undump(const char*s);
const char*sp_str_succ_impl(const char*s);
const char*sp_str_succ(const char*s);
sp_StrArray*sp_str_split(const char*s,const char*sep);
sp_StrArray*sp_str_split_drop_trailing(const char*s,const char*sep);
sp_StrArray*sp_str_split_limit(const char*s,const char*sep,mrb_int n);
sp_StrArray*sp_str_split_ws(const char*s);
sp_StrArray*sp_str_split_ws_limit(const char*s,mrb_int n);
sp_StrArray*sp_str_scan(const char*s,const char*pat);
const char*sp_str_gsub(const char*s,const char*pat,const char*rep);
mrb_int sp_str_index(const char*s,const char*sub);
mrb_int sp_str_index_from(const char*s,const char*sub,mrb_int start);
mrb_int sp_str_rindex(const char*s,const char*sub);
mrb_int sp_str_rindex_from(const char*s,const char*sub,mrb_int pos);
mrb_int sp_str_byteindex(const char*s,const char*sub);
mrb_int sp_str_byteindex_from(const char*s,const char*sub,mrb_int start);
mrb_int sp_str_byterindex(const char*s,const char*sub);
mrb_int sp_str_byterindex_from(const char*s,const char*sub,mrb_int pos);
const char*sp_str_sub_range(const char*s,mrb_int start,mrb_int len);
const char*sp_str_char_at_or_nil(const char*s,mrb_int i);
const char*sp_str_sub_range_len(const char*s,mrb_int cl,mrb_int start,mrb_int len);
const char*sp_str_sub_range_r(const char*s,mrb_int start,mrb_int end_,mrb_int excl);
const char*sp_str_sub_range_len_r(const char*s,mrb_int cl,mrb_int start,mrb_int end_,mrb_int excl);
const char*sp_str_reverse(const char*s);
mrb_int sp_str_count(const char*s,const char*chars);
mrb_int sp_str_count_n(const char*s,const char**chars,mrb_int n);
sp_IntArray*sp_str_codepoints(const char*s);
sp_StrArray*sp_str_chars(const char*s);
const char*sp_str_tr(const char*s,const char*from,const char*to);
const char*sp_str_tr_s(const char*s,const char*from,const char*to);
const char*sp_str_delete(const char*s,const char*chars);
const char*sp_str_squeeze(const char*s);
const char*sp_str_squeeze_chars(const char*s,const char*cs);
const char*sp_str_delete_n(const char*s,const char**chars,mrb_int n);
const char*sp_str_squeeze_n(const char*s,const char**chars,mrb_int n);
const char *sp_str_scrub(const char *s, const char *repl);
const char*sp_str_ljust(const char*s,mrb_int w);
const char*sp_str_rjust(const char*s,mrb_int w);
const char*sp_str_center(const char*s,mrb_int w);
const char*sp_str_ljust2(const char*s,mrb_int w,const char*pad);
const char*sp_str_rjust2(const char*s,mrb_int w,const char*pad);
const char*sp_str_center2(const char*s,mrb_int w,const char*pad);
mrb_int sp_str_index_opt(const char *s, const char *sub);
mrb_int sp_str_index_from_opt(const char *s, const char *sub, mrb_int start);
mrb_int sp_str_rindex_opt(const char *s, const char *sub);

#endif /* SP_STR_H */
