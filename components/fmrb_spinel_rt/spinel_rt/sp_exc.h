#ifndef SP_EXC_H
#define SP_EXC_H
/* sp_exc.h -- sp_Exception struct + cold ops.
 *
 * sp_user_exc_parent_fn is a function-pointer HOOK (like
 * sp_gc_mark_globals_hook): codegen's emitted main() sets it to the
 * per-program sp_user_exc_parent (`sp_user_exc_parent_fn =
 * sp_user_exc_parent;`), which stays static/resident since its body is
 * program-specific. Everything that reaches the user exception hierarchy
 * only needs the POINTER, resolved at runtime -- not the per-program
 * function itself -- so it's a real cross-archive dependency, not a
 * program-generated-code blocker. sp_pending_exc_recv/key/val/flags (the
 * raise-site introspection staging slots) are the same shape: already
 * SP_TLS globals, just needed `static` removed.
 *
 * sp_exc_sym_slot/sp_exc_recover_named (need sp_sym_intern, a REAL
 * program-generated function whose body differs per compiled program --
 * not a hook) and sp_exc_is_a (poly value-dispatch) stay in spinel_rt.h,
 * along with sp_exc_reason_acc/sp_exc_tag_acc (the two accessors that
 * call sp_exc_sym_slot) and the raise/longjmp control flow
 * (sp_raise_exc/sp_raise_cls and friends), which threads through the
 * TU-local exception-stack globals.
 *
 * 0 optcarrot uses for every function below.
 */
#include "sp_types.h"   /* sp_int, sp_bool */
#include "sp_gc.h"      /* sp_RbVal, sp_gc_alloc, sp_gc_mark, sp_mark_string */
#include "sp_alloc.h"   /* sp_box_nil/int, sp_str_empty, sp_raise_cls, sp_sprintf */
#include <signal.h>     /* SIGINT for sp_exc_new's Interrupt#signo default */

typedef struct sp_Exception_s {
  const char *cls_name;
  const char *parent_cls_name; /* builtin ancestor for user subclasses, or NULL */
  const char *msg;
  struct sp_Exception_s *cause; /* the exception being handled when this was raised, or NULL */
  sp_RbVal result;             /* StopIteration#result (the iteration's return value); nil otherwise */
  sp_RbVal xname;              /* NameError/NoMethodError#name (the missing name); nil otherwise */
  sp_RbVal xkey;               /* KeyError#key / UncaughtThrowError#tag / LocalJumpError#reason /
                                  NoMethodError#args; nil otherwise */
  sp_RbVal xrecv;              /* KeyError/NameError/NoMethodError/FrozenError#receiver; nil otherwise */
  sp_bool has_recv;           /* was a receiver actually recorded? nil is a legal
                                  receiver (nil.foo), so the box cannot say (#3036) */
  sp_bool has_key;            /* likewise for KeyError#key (#3030) */
  sp_bool priv_call;          /* NoMethodError#private_call? (#3042) */
  sp_StrArray *backtrace;     /* an attached backtrace set by #set_backtrace; nil
                                 means "no caller ever set one" (CRuby answers
                                 nil for the unset case). The GC mark visits it
                                 alongside the boxed fields. */
} sp_Exception;

#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
extern const char *(*sp_user_exc_parent_fn)(const char *);   /* set by the generated main() */
#endif
/* The modules a class includes, NULL-terminated, or NULL for none. Ruby's type
   hierarchy is not a chain -- `rescue IO::WaitReadable` and `e.is_a?(Retryable)`
   both ask about a MODULE, which a single-parent walk cannot answer. Same shape
   as the parent hook: a builtin table plus a per-program hook. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
extern const char *const *(*sp_user_exc_modules_fn)(const char *);
#endif
const char *const *sp_exc_modules_of_name(const char *cls);
/* CRuby aliases some exception classes to one object (Errno::EWOULDBLOCK IS
   Errno::EAGAIN). Names cannot express identity, so both sides of a match are
   canonicalized first. Returns `cls` itself when it has no alias. */
const char *sp_exc_canonical_name(const char *cls);
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
extern SP_TLS sp_RbVal sp_pending_exc_recv, sp_pending_exc_key, sp_pending_exc_val;
extern SP_TLS unsigned char sp_pending_exc_flags;
#endif

int sp_exc_cls_matches(const char *raised, const char *target);
SP_COLD void sp_exc_acc_gate(sp_Exception *e, const char *cls, const char *acc);
int sp_exc_is_standard_error(const char *raised);
sp_Exception *sp_exc_new_for_catch(const char *cls, const char *msg);
/* The message a bare `raise` carries: empty, and distinct from "no message
   given" (which falls back to the class name, as Exception.new does) (#3711). */
extern const char *const sp_exc_no_msg;
/* An explicitly given raise message: an empty one stays empty rather than
   falling back to the class name the way a message-less raise does. */
static inline const char *sp_exc_msg_given(const char *m) {
  return (m && !m[0]) ? sp_exc_no_msg : m;
}
void *sp_exc_new_sub_sized(size_t sz, const char *cls_name, const char *msg);

void sp_exc_gc_scan(void *p);
sp_Exception *sp_exc_new(const char *cls_name, const char *msg);
sp_bool sp_exc_eq(sp_Exception *a, sp_Exception *b);
/* Exception#set_backtrace: attach (or replace) the backtrace array. The
 * StrArray is GC-managed; the sp_Exception holds a single owner reference
 * and is updated in place. Returns the boxed value so chained assignments
 * (`e = e.set_backtrace(bt)`) work. */
sp_RbVal sp_Exception_set_backtrace(sp_Exception *e, sp_StrArray *bt);
sp_Exception *sp_exc_new_sub(const char *cls_name, const char *parent_cls, const char *msg);
sp_Exception *sp_exc_dup(sp_Exception *e);
void *sp_exc_apply_staged(const char *cls, const char *msg, void *obj);
int sp_exc_exit_status(void *obj);
sp_Exception *sp_exc_exception(sp_Exception *e, const char *msg);
const char *sp_exc_class_name(volatile sp_Exception *ve);
const char *sp_exc_message(volatile sp_Exception *ve);
const char *sp_exc_inspect(void *p);   /* #<Cls: msg>, for the inspect dispatch */
sp_Exception *sp_exc_cause(volatile sp_Exception *ve);
sp_RbVal sp_exc_result(volatile sp_Exception *ve);
const char *sp_errno_class_name(int e);   /* "Errno::ENOENT" for ENOENT; the parent for an unlisted one */
sp_RbVal sp_exc_errno_acc(sp_Exception *e);   /* SystemCallError#errno */
sp_int sp_errno_num(const char *cls);   /* Errno::ENOENT::Errno */
const char *sp_exc_parent_of_name(const char *cls);
sp_RbVal sp_exc_name_acc(sp_Exception *e);
sp_RbVal sp_exc_key_acc(sp_Exception *e);
sp_RbVal sp_exc_receiver_acc(sp_Exception *e);
sp_RbVal sp_exc_args_acc(sp_Exception *e);
sp_bool sp_exc_private_call_acc(sp_Exception *e);
sp_RbVal sp_exc_exit_value_acc(sp_Exception *e);
sp_RbVal sp_exc_throw_value_acc(sp_Exception *e);
sp_int sp_exc_status_acc(sp_Exception *e);
sp_bool sp_exc_success_acc(sp_Exception *e);
sp_int sp_exc_signo_acc(sp_Exception *e);
const char *sp_exc_signm_acc(sp_Exception *e);

/* ---- Signal/Interrupt exception constructors: relocated from
   spinel_rt.h (0 optcarrot uses). sp_signal_resolve/sp_signal_signame
   are already non-static (resolved at the final link). ---- */
#ifndef SP_MULTI_CTX  /* per-ctx macro under SP_MULTI_CTX (sp_ctx.h) */
int sp_signal_resolve(sp_RbVal sig);
#endif
#ifndef SP_MULTI_CTX  /* per-ctx macro under SP_MULTI_CTX (sp_ctx.h) */
const char *sp_signal_signame(sp_int no);
#endif
sp_Exception *sp_signal_exc_new_m(sp_RbVal sig, const char *msg);
sp_Exception *sp_signal_exc_new(sp_RbVal sig);
sp_Exception *sp_interrupt_new(const char *msg);

#endif
