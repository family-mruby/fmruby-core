#ifndef SP_IOBUFFER_H
#define SP_IOBUFFER_H
/* IO::Buffer -- a fixed-size byte buffer with typed numeric accessors,
   following CRuby 4.x semantics (lowercase type symbols are little-endian,
   uppercase big-endian; U8/S8 are single bytes). The class surface is bound
   through the native-class DSL in packages/io/io/buffer.rb; the compiler
   splices that file whenever a program references `IO::Buffer` (the same
   implicit-require treatment `Set` gets), so the class is available without
   an explicit require, as in CRuby.

   Memory model: a buffer owns a malloc'd allocation (`data`), freed by the
   GC finalizer. A slice owns nothing: it holds its root buffer in `source`
   (marked by the GC scan hook, so the backing allocation outlives every
   view) and resolves its base pointer through `source->data + off` on every
   access -- a source resize can move the allocation and the views stay
   valid, where CRuby's stored-pointer slices would dangle. */
#include "sp_types.h"
#include "sp_gc.h"
#include "sp_alloc.h"

/* Flag bits: the public constants share CRuby's values; SLICE is CRuby's
   internal display-only flag (no public constant). */
#define SP_IOB_EXTERNAL 1u
#define SP_IOB_INTERNAL 2u
#define SP_IOB_MAPPED   4u
#define SP_IOB_SHARED   8u
#define SP_IOB_LOCKED   32u
#define SP_IOB_PRIVATE  64u
#define SP_IOB_READONLY 128u
#define SP_IOB_SLICE    (1u << 16)

typedef struct sp_IOBuffer_s sp_IOBuffer;
struct sp_IOBuffer_s {
  sp_int cls_id;             /* object header: runtime class id, compiler-stamped */
  uint8_t *data;             /* owned allocation; NULL for null buffers and slices */
  sp_IOBuffer *source;       /* slice: the root (non-slice) buffer, GC-marked */
  int64_t off;               /* slice: byte offset into source */
  int64_t size;
  uint32_t flags;
  void *map_base;            /* IO::Buffer.map: the mmap'd region `data` points into
                                (page-aligned), NULL for a malloc'd allocation */
  size_t map_len;            /* ...and its length, for munmap */
};

/* The typed-accessor type enum. Order groups by width; *_BE are the
   uppercase (big-endian) spellings. Mirrored by the SP_IOB_TY_* defines in
   spinel_rt.h that the compiler's literal-symbol fast path emits. */
enum {
  SP_IOB_TY_U8, SP_IOB_TY_S8,
  SP_IOB_TY_u16, SP_IOB_TY_s16, SP_IOB_TY_U16, SP_IOB_TY_S16,
  SP_IOB_TY_u32, SP_IOB_TY_s32, SP_IOB_TY_U32, SP_IOB_TY_S32,
  SP_IOB_TY_u64, SP_IOB_TY_s64, SP_IOB_TY_U64, SP_IOB_TY_S64,
  SP_IOB_TY_f32, SP_IOB_TY_f64, SP_IOB_TY_F32, SP_IOB_TY_F64,
  SP_IOB_TY__COUNT
};

/* constructors (cls_id first: the compiler stamps the assigned class id) */
sp_IOBuffer *sp_IOBuffer_new(sp_int cls_id);
sp_IOBuffer *sp_IOBuffer_new_i(sp_int cls_id, sp_int size);
sp_IOBuffer *sp_IOBuffer_new_if(sp_int cls_id, sp_int size, sp_int flags);
void sp_IOBuffer_fin(void *p);

/* generic (runtime-dispatched type symbol) accessors */
sp_RbVal sp_IOBuffer_get_value(sp_IOBuffer *b, sp_RbVal type, sp_int off);
sp_int sp_IOBuffer_set_value(sp_IOBuffer *b, sp_RbVal type, sp_int off, sp_RbVal val);

/* typed fast paths, called by the compiler's literal-symbol lowering */
sp_int sp_IOBuffer_get_i(sp_IOBuffer *b, sp_int ty, sp_int off);
sp_RbVal sp_IOBuffer_get_x(sp_IOBuffer *b, sp_int ty, sp_int off);
double sp_IOBuffer_get_f(sp_IOBuffer *b, sp_int ty, sp_int off);
sp_int sp_IOBuffer_set_i(sp_IOBuffer *b, sp_int ty, sp_int off, sp_int v);
sp_int sp_IOBuffer_set_f(sp_IOBuffer *b, sp_int ty, sp_int off, double v);
sp_int sp_IOBuffer_set_v(sp_IOBuffer *b, sp_int ty, sp_int off, sp_RbVal v);

/* strings */
const char *sp_IOBuffer_get_string0(sp_IOBuffer *b);
const char *sp_IOBuffer_get_string1(sp_IOBuffer *b, sp_int off);
const char *sp_IOBuffer_get_string2(sp_IOBuffer *b, sp_int off, sp_int len);
sp_int sp_IOBuffer_set_string1(sp_IOBuffer *b, const char *s);
sp_int sp_IOBuffer_set_string2(sp_IOBuffer *b, const char *s, sp_int off);
sp_int sp_IOBuffer_set_string3(sp_IOBuffer *b, const char *s, sp_int off, sp_int len);
sp_int sp_IOBuffer_set_string4(sp_IOBuffer *b, const char *s, sp_int off, sp_int len, sp_int soff);

/* whole-buffer operations */
sp_int sp_IOBuffer_size(sp_IOBuffer *b);
sp_IOBuffer *sp_IOBuffer_resize(sp_IOBuffer *b, sp_int size);
sp_IOBuffer *sp_IOBuffer_clear0(sp_IOBuffer *b);
sp_IOBuffer *sp_IOBuffer_clear1(sp_IOBuffer *b, sp_int v);
sp_IOBuffer *sp_IOBuffer_clear2(sp_IOBuffer *b, sp_int v, sp_int off);
sp_IOBuffer *sp_IOBuffer_clear3(sp_IOBuffer *b, sp_int v, sp_int off, sp_int len);
sp_int sp_IOBuffer_copy1(sp_IOBuffer *b, sp_RbVal src);
sp_int sp_IOBuffer_copy2(sp_IOBuffer *b, sp_RbVal src, sp_int off);
sp_int sp_IOBuffer_copy3(sp_IOBuffer *b, sp_RbVal src, sp_int off, sp_int len);
sp_int sp_IOBuffer_copy4(sp_IOBuffer *b, sp_RbVal src, sp_int off, sp_int len, sp_int soff);
sp_IOBuffer *sp_IOBuffer_slice0(sp_IOBuffer *b);
sp_IOBuffer *sp_IOBuffer_slice1(sp_IOBuffer *b, sp_int off);
sp_IOBuffer *sp_IOBuffer_slice2(sp_IOBuffer *b, sp_int off, sp_int len);
sp_IOBuffer *sp_IOBuffer_transfer(sp_IOBuffer *b);
sp_IOBuffer *sp_IOBuffer_free_m(sp_IOBuffer *b);
sp_IOBuffer *sp_IOBuffer_dup_m(sp_IOBuffer *b);
sp_IOBuffer *sp_IOBuffer_become_for(sp_IOBuffer *b, const char *s);

/* comparison */
sp_int sp_IOBuffer_cmp(sp_IOBuffer *b, sp_RbVal other);
sp_bool sp_IOBuffer_eq(sp_IOBuffer *b, sp_RbVal other);

/* rendering */
const char *sp_IOBuffer_hexdump0(sp_IOBuffer *b);
const char *sp_IOBuffer_hexdump1(sp_IOBuffer *b, sp_int off);
const char *sp_IOBuffer_hexdump2(sp_IOBuffer *b, sp_int off, sp_int len);
const char *sp_IOBuffer_hexdump3(sp_IOBuffer *b, sp_int off, sp_int len, sp_int width);
const char *sp_IOBuffer_inspect(sp_IOBuffer *b);
const char *sp_IOBuffer_to_s(sp_IOBuffer *b);

/* predicates */
sp_bool sp_IOBuffer_null_p(sp_IOBuffer *b);
sp_bool sp_IOBuffer_empty_p(sp_IOBuffer *b);
sp_bool sp_IOBuffer_valid_p(sp_IOBuffer *b);
sp_bool sp_IOBuffer_external_p(sp_IOBuffer *b);
sp_bool sp_IOBuffer_internal_p(sp_IOBuffer *b);
sp_bool sp_IOBuffer_mapped_p(sp_IOBuffer *b);
sp_bool sp_IOBuffer_shared_p(sp_IOBuffer *b);
sp_bool sp_IOBuffer_locked_p(sp_IOBuffer *b);
sp_bool sp_IOBuffer_readonly_p(sp_IOBuffer *b);
sp_bool sp_IOBuffer_private_p(sp_IOBuffer *b);

/* bitwise */
sp_IOBuffer *sp_IOBuffer_and(sp_IOBuffer *b, sp_RbVal other);
sp_IOBuffer *sp_IOBuffer_or(sp_IOBuffer *b, sp_RbVal other);
sp_IOBuffer *sp_IOBuffer_xor(sp_IOBuffer *b, sp_RbVal other);
sp_IOBuffer *sp_IOBuffer_not(sp_IOBuffer *b);
sp_IOBuffer *sp_IOBuffer_and_ip(sp_IOBuffer *b, sp_RbVal other);
sp_IOBuffer *sp_IOBuffer_or_ip(sp_IOBuffer *b, sp_RbVal other);
sp_IOBuffer *sp_IOBuffer_xor_ip(sp_IOBuffer *b, sp_RbVal other);
sp_IOBuffer *sp_IOBuffer_not_ip(sp_IOBuffer *b);

/* locked { } support (the Ruby-side `locked` wraps these) */
sp_IOBuffer *sp_IOBuffer_lock(sp_IOBuffer *b);
sp_IOBuffer *sp_IOBuffer_unlock(sp_IOBuffer *b);

/* IO integration (#4474): one read(2) / write(2) / pread(2) / pwrite(2)
   against a boxed IO handle, answering the byte count, 0 at EOF, or
   -errno as CRuby does. `length` < 0 stands for nil (the rest of the
   buffer from `offset`). The Ruby side holds the lock around them. */
sp_int sp_IOBuffer_read_io(sp_IOBuffer *b, sp_RbVal io, sp_int length, sp_int offset);
sp_int sp_IOBuffer_write_io(sp_IOBuffer *b, sp_RbVal io, sp_int length, sp_int offset);
sp_int sp_IOBuffer_pread_io(sp_IOBuffer *b, sp_RbVal io, sp_int from, sp_int length, sp_int offset);
sp_int sp_IOBuffer_pwrite_io(sp_IOBuffer *b, sp_RbVal io, sp_int from, sp_int length, sp_int offset);
/* IO::Buffer.map(file, size, offset, flags): the receiver (a fresh null
   buffer) becomes a view of the file's bytes through mmap; `size` < 0
   stands for nil (the file's size from `offset`). */
sp_IOBuffer *sp_IOBuffer_become_map(sp_IOBuffer *b, sp_RbVal io, sp_int size, sp_int offset, sp_int flags);

/* FFI: the base address C receives for a buffer in an ffi_func pointer
   argument; `writing` is 0 only for :buffer_in. The _ptr form takes a boxed
   argument and passes anything but an IO::Buffer through as its pointer. */
void *sp_IOBuffer_ffi_base(sp_IOBuffer *b, sp_int writing);
void *sp_IOBuffer_ffi_ptr(sp_RbVal v, sp_int cls_id, sp_int writing);
sp_int sp_IOBuffer_ffi_hold(sp_IOBuffer *b);
void sp_IOBuffer_ffi_release(sp_IOBuffer *b, sp_int m);
sp_int sp_IOBuffer_ffi_hold_v(sp_RbVal v, sp_int cls_id);
void sp_IOBuffer_ffi_release_v(sp_RbVal v, sp_int cls_id, sp_int m);

/* IO::Buffer::PAGE_SIZE (the mapped-allocation threshold) */
sp_int sp_IOBuffer_page_size(void);

#endif /* SP_IOBUFFER_H */
