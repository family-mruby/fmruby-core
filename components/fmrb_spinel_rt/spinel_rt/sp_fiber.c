/* sp_fiber.c -- Fiber runtime bodies (POSIX ucontext).
 * See sp_fiber.h. The collector roots, sp_gc_alloc, and sp_raise_cls live
 * in the generated TU and are reached by name; fiber-local storage is
 * self-contained here. */
#include "sp_fiber.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* OpenBSD requires the stack a coroutine runs on to be MAP_STACK, else the
   first push faults. Linux defines MAP_STACK as a (currently) no-op; keep it
   portable. */
#ifndef MAP_STACK
#define MAP_STACK 0
#endif

/* A PROT_NONE guard page below the usable stack: the coroutine stack grows down
   from base+size toward base (see sp_ctx_make), so an overflow runs into the
   guard and faults cleanly instead of silently corrupting whatever mapping sits
   below. f->stack points at the mmap base (guard + stack); the usable region the
   context switch runs on starts sp_fiber_guard() bytes in. Sized to one page
   (queried so 16K/64K-page arm64 hosts stay aligned). */
static size_t sp_fiber_guard(void) {
  static size_t g = 0;
  if (!g) { long p = sysconf(_SC_PAGESIZE); g = p > 0 ? (size_t)p : 4096; }
  return g;
}

/* ---- Portable coroutine context switch (replaces swapcontext) ----
   sp_ctx_swap saves the callee-saved registers onto the *current* stack, stows
   the resulting stack pointer in *from, loads *to's stack pointer, and pops the
   callee-saved registers (the very first switch into a coroutine pops the zeroed
   slots sp_ctx_make laid out and `ret`s into the trampoline). It lives in .text
   (W^X-safe) and issues no syscall. See sp_fiber_ctx.h for sp_ctx_make and the
   stack layout it must mirror. */
#if SP_FIBER_ASM
#if defined(__APPLE__)
  #define SP_CTX_SYM "_sp_ctx_swap"
#else
  #define SP_CTX_SYM "sp_ctx_swap"
#endif
#if defined(__x86_64__)
__asm__(
  ".text\n"
  ".globl " SP_CTX_SYM "\n"
  SP_CTX_SYM ":\n"
  "  pushq %rbp\n  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
  "  movq %rsp, (%rdi)\n"        /* from->sp = rsp                      */
  "  movq (%rsi), %rsp\n"        /* rsp = to->sp                        */
  "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n  popq %rbp\n"
  "  ret\n"
);
#elif defined(__aarch64__)
__asm__(
  ".text\n"
  ".globl " SP_CTX_SYM "\n"
  SP_CTX_SYM ":\n"
  /* save x19..x30 (12*8) + d8..d15 (8*8) = 160 bytes onto the current stack */
  "  stp x19, x20, [sp, #-160]!\n"
  "  stp x21, x22, [sp, #16]\n"
  "  stp x23, x24, [sp, #32]\n"
  "  stp x25, x26, [sp, #48]\n"
  "  stp x27, x28, [sp, #64]\n"
  "  stp x29, x30, [sp, #80]\n"
  "  stp d8,  d9,  [sp, #96]\n"
  "  stp d10, d11, [sp, #112]\n"
  "  stp d12, d13, [sp, #128]\n"
  "  stp d14, d15, [sp, #144]\n"
  "  mov x2, sp\n"
  "  str x2, [x0]\n"             /* from->sp = sp */
  "  ldr x2, [x1]\n"            /* sp = to->sp   */
  "  mov sp, x2\n"
  "  ldp d14, d15, [sp, #144]\n"
  "  ldp d12, d13, [sp, #128]\n"
  "  ldp d10, d11, [sp, #112]\n"
  "  ldp d8,  d9,  [sp, #96]\n"
  "  ldp x29, x30, [sp, #80]\n"
  "  ldp x27, x28, [sp, #64]\n"
  "  ldp x25, x26, [sp, #48]\n"
  "  ldp x23, x24, [sp, #32]\n"
  "  ldp x21, x22, [sp, #16]\n"
  "  ldp x19, x20, [sp], #160\n"
  "  ret\n"
);
#endif
#else  /* ucontext fallback */
void sp_ctx_swap(sp_fiber_ctx *from, sp_fiber_ctx *to) { swapcontext(&from->uc, &to->uc); }
#endif

/* ---- Reached by name in the generated TU ---- */
void *sp_gc_alloc(size_t sz, void (*fin)(void *), void (*scn)(void *));
SP_NORETURN void sp_raise_cls(const char *cls, const char *msg);

/* Per-fiber exception/catch handler context (#1474): defined in the generated
   TU (sp_runtime.h), reached here by name. A fiber's begin/rescue handlers and
   catch tags are saved/restored around each context switch so a raise/throw
   can't longjmp into a suspended fiber's stack frame, and an unhandled fiber
   raise is re-raised in the resumer's context rather than crossing stacks. */
#include <setjmp.h>
void *sp_exc_ctx_new(void);
void sp_exc_ctx_free(void *p);
void sp_exc_ctx_save(void *p);
void sp_exc_ctx_load(void *p);
void sp_exc_ctx_mark(void *p);
void sp_exc_arm(jmp_buf b);
void sp_exc_disarm(void);
const char *sp_exc_cur_cls(void);
const char *sp_exc_cur_msg(void);
void *sp_exc_cur_obj(void);
void sp_fiber_reraise(const char *cls, const char *msg, void *obj);

static inline sp_RbVal sp_box_nil(void) { sp_RbVal r; r.tag = SP_TAG_NIL; r.cls_id = 0; r.v.i = 0; return r; }
static void sp_raise(const char *msg) { sp_raise_cls("RuntimeError", msg); }

/* Fiber-local variable storage (Fiber#[] / Fiber#[]=). A small sym->RbVal
   map, GC-allocated so the collector marks its values via the scan hook;
   self-contained here rather than borrowing the runtime's sym_poly_hash so
   the runtime sym_poly_hash stays collector-private to the generated TU. */
typedef struct { sp_sym *keys; sp_RbVal *vals; mrb_int len, cap; } sp_FiberStore;
static void sp_FiberStore_scan(void *p) { sp_FiberStore *s = (sp_FiberStore *)p; for (mrb_int i = 0; i < s->len; i++) sp_mark_rbval(s->vals[i]); }
static void sp_FiberStore_fin(void *p) { sp_FiberStore *s = (sp_FiberStore *)p; free(s->keys); free(s->vals); }
static sp_FiberStore *sp_FiberStore_new(void) {
  sp_FiberStore *s = (sp_FiberStore *)sp_gc_alloc(sizeof(sp_FiberStore), sp_FiberStore_fin, sp_FiberStore_scan);
  s->cap = 8; s->len = 0;
  s->keys = (sp_sym *)malloc(sizeof(sp_sym) * s->cap);
  s->vals = (sp_RbVal *)malloc(sizeof(sp_RbVal) * s->cap);
  if (!s->keys || !s->vals) {
    /* If one side succeeded, release it now rather than waiting for the
       finalizer -- and NULL both so that finalizer's free stays a no-op. */
    free(s->keys); free(s->vals);
    s->keys = NULL; s->vals = NULL;
    sp_raise_cls("NoMemoryError", "failed to allocate fiber storage");
  }
  return s;
}
static sp_RbVal sp_FiberStore_get(sp_FiberStore *s, sp_sym k) {
  for (mrb_int i = 0; i < s->len; i++) if (s->keys[i] == k) return s->vals[i];
  return sp_box_nil();
}
static void sp_FiberStore_set(sp_FiberStore *s, sp_sym k, sp_RbVal v) {
  for (mrb_int i = 0; i < s->len; i++) if (s->keys[i] == k) { s->vals[i] = v; return; }
  if (s->len == s->cap) {
    /* Grow each array into a temp and commit only on success, so a failed
       realloc leaves the store intact (the old buffer survives) instead of
       NULL-deref'ing on the write below or double-freeing a moved buffer. */
    mrb_int nc = s->cap * 2;
    sp_sym *nk = (sp_sym *)realloc(s->keys, sizeof(sp_sym) * nc);
    if (!nk) sp_raise_cls("NoMemoryError", "failed to grow fiber storage");
    s->keys = nk;
    sp_RbVal *nv = (sp_RbVal *)realloc(s->vals, sizeof(sp_RbVal) * nc);
    if (!nv) sp_raise_cls("NoMemoryError", "failed to grow fiber storage");
    s->vals = nv; s->cap = nc;
  }
  s->keys[s->len] = k; s->vals[s->len] = v; s->len++;
}
static sp_FiberStore *sp_FiberStore_dup(sp_FiberStore *o) {
  sp_FiberStore *s = sp_FiberStore_new();
  for (mrb_int i = 0; i < o->len; i++) sp_FiberStore_set(s, o->keys[i], o->vals[i]);
  return s;
}

/* ThreadSanitizer fiber instrumentation (see sp_fiber.h SP_TSAN). Each fiber
   gets a __tsan fiber handle; before every context switch we tell TSan which
   fiber will run next so its happens-before tracking follows the cooperative
   switch instead of choking on the asm stack swap. No-ops when not building
   under -fsanitize=thread. */
#ifdef SP_TSAN
extern void *__tsan_get_current_fiber(void);
extern void *__tsan_create_fiber(unsigned flags);
extern void  __tsan_destroy_fiber(void *fiber);
extern void  __tsan_switch_to_fiber(void *fiber, unsigned flags);
#define SP_TSAN_SET_CALLER(f, who) ((f)->caller_fiber = (who))
#define SP_TSAN_SWITCH(to)         do { sp_Fiber *_st = (to); if (_st && _st->tsan_fiber) __tsan_switch_to_fiber(_st->tsan_fiber, 0); } while (0)
#else
#define SP_TSAN_SET_CALLER(f, who) ((void)0)
#define SP_TSAN_SWITCH(to)         ((void)0)
#endif

/* The root fiber is a static, not a GC allocation — but `Fiber.current`
   hands it to user code, where a rooted local makes the collector mark
   it. Lay a 0xfd skip byte directly before it so sp_gc_mark's tag-byte
   protocol bails out instead of treating .bss as a GC header. The guard
   array is exactly one alignment unit, so its last byte always directly
   precedes `root` (no compiler padding can slip in between). */
/* In the threaded build the root fiber is per-worker (SP_TLS): each OS worker's
   native scheduler stack is its own root coroutine, so the green thread running
   on a worker transfers back to *that* worker's root. The 0xfd guard byte still
   directly precedes `root` in each thread's TLS block, so sp_gc_mark's tag-byte
   protocol skips it there too. sp_fiber_current cannot be statically initialized
   to &sp_fiber_root once the root is thread-local (that address is not a
   constant), so each worker sets it via sp_fiber_worker_init before running any
   fiber; the single-threaded build keeps the constant static init unchanged
   (byte-identical). */
static SP_TLS struct { char guard[_Alignof(sp_Fiber)]; sp_Fiber root; } sp_fiber_root_box
    = { .guard = { [_Alignof(sp_Fiber) - 1] = (char)0xfd }, .root = {0} };
#define sp_fiber_root (sp_fiber_root_box.root)
#ifdef SP_THREADS
SP_TLS sp_Fiber *sp_fiber_current = NULL;              /* set by sp_fiber_worker_init */
#else
SP_TLS sp_Fiber *sp_fiber_current = &sp_fiber_root;    /* extern: read by the generated TU */
#endif

/* Adopt the calling OS thread's root fiber as its current coroutine. Called once
   per worker before it runs any green thread (worker 0 from sp_sched_init; helper
   workers from their startup). A no-op-shaped reset in the single-threaded build
   (sp_fiber_current already equals &sp_fiber_root). */
void sp_fiber_worker_init(void) { sp_fiber_current = &sp_fiber_root; }
sp_Fiber *sp_fiber_worker_root(void) { return &sp_fiber_root; }
static sp_Fiber *sp_fiber_list_head = NULL;

static void sp_fiber_save_roots(sp_Fiber*f){if(f->saved_roots_cap<sp_gc_nroots){int nc=sp_gc_nroots>64?sp_gc_nroots*2:64;void***nx=(void***)realloc(f->saved_roots,sizeof(void**)*nc);if(!nx)return;f->saved_roots=nx;f->saved_roots_cap=nc;}if(sp_gc_nroots>0)memcpy(f->saved_roots,sp_gc_roots,sizeof(void**)*sp_gc_nroots);f->saved_nroots=sp_gc_nroots;}
static void sp_fiber_restore_roots(sp_Fiber*f){if(f->saved_nroots>0)memcpy(sp_gc_roots,f->saved_roots,sizeof(void**)*f->saved_nroots);sp_gc_nroots=f->saved_nroots;}
/* Snapshot the calling worker's live shadow-stack roots into the green thread it
   is running, so a stop-the-world collector can mark them while this worker is
   parked at a safepoint. The collector reaches them via the suspended-fibers GC
   hook (every fiber but the collector's own current is marked from saved_roots). */
void sp_fiber_publish_current_roots(void){if(sp_fiber_current)sp_fiber_save_roots(sp_fiber_current);}
/* The global fiber list is mutated by Fiber.new from any worker (list_add) and
   walked by the collector during stop-the-world (all mutators parked, so the
   walk needs no lock). A small mutex serializes concurrent list_add/remove; the
   critical section is a few pointer writes with no safepoint poll inside, so a
   worker never parks holding it and the collector never waits on it. */
#ifdef SP_THREADS
#include <pthread.h>
static pthread_mutex_t sp_fiber_list_lock = PTHREAD_MUTEX_INITIALIZER;
#define FIBER_LIST_LOCK()   pthread_mutex_lock(&sp_fiber_list_lock)
#define FIBER_LIST_UNLOCK() pthread_mutex_unlock(&sp_fiber_list_lock)
#else
#define FIBER_LIST_LOCK()   ((void)0)
#define FIBER_LIST_UNLOCK() ((void)0)
#endif
static void sp_fiber_list_add(sp_Fiber*f){FIBER_LIST_LOCK();f->fiber_prev=NULL;f->fiber_next=sp_fiber_list_head;if(sp_fiber_list_head)sp_fiber_list_head->fiber_prev=f;sp_fiber_list_head=f;FIBER_LIST_UNLOCK();}
static void sp_fiber_list_remove(sp_Fiber*f){FIBER_LIST_LOCK();if(f->fiber_prev)f->fiber_prev->fiber_next=f->fiber_next;else if(sp_fiber_list_head==f)sp_fiber_list_head=f->fiber_next;if(f->fiber_next)f->fiber_next->fiber_prev=f->fiber_prev;f->fiber_prev=NULL;f->fiber_next=NULL;FIBER_LIST_UNLOCK();}
/* Mark a fiber's published (saved) roots and pending-exception objects. Public
   so the thread scheduler can mark a parked worker's per-worker root fiber, which
   is not on sp_fiber_list_head (only Fiber.new fibers are) and so would otherwise
   be missed when a *different* worker is the stop-the-world collector. */
void sp_fiber_mark_roots(sp_Fiber*f){int i;for(i=0;i<f->saved_nroots;i++){void**e=f->saved_roots[i];if((uintptr_t)e&(uintptr_t)3){sp_gc_mark_root_entry(e);}else{void*obj=*e;if(obj)sp_gc_mark(obj);}}if(f->exc_ctx)sp_exc_ctx_mark(f->exc_ctx);if(f->raised_obj)sp_gc_mark(f->raised_obj);if(f->inj_obj)sp_gc_mark(f->inj_obj);}
static void sp_mark_fiber_roots(sp_Fiber*f){if(f==sp_fiber_current)return;sp_fiber_mark_roots(f);}
static void sp_mark_suspended_fibers(void){sp_mark_fiber_roots(&sp_fiber_root);sp_Fiber*f=sp_fiber_list_head;while(f){sp_mark_fiber_roots(f);f=f->fiber_next;}}
static void sp_fiber_install_gc_hook(void){if(!sp_gc_mark_suspended_fibers_hook)sp_gc_mark_suspended_fibers_hook=sp_mark_suspended_fibers;}
static void sp_Fiber_fin(void*p){sp_Fiber*f=(sp_Fiber*)p;if(f->stack)munmap(f->stack,sp_fiber_guard()+SP_FIBER_STACK_SIZE);if(f->saved_roots)free(f->saved_roots);if(f->exc_ctx)sp_exc_ctx_free(f->exc_ctx);
#ifdef SP_TSAN
  if(f->tsan_fiber)__tsan_destroy_fiber(f->tsan_fiber);
#endif
  sp_fiber_list_remove(f);}
static void sp_Fiber_scan(void*p){sp_Fiber*f=(sp_Fiber*)p;if(f->user_data)sp_gc_mark(f->user_data);if(f->storage)sp_gc_mark(f->storage);}
sp_Fiber*sp_Fiber_new(void(*body)(sp_Fiber*)){sp_Fiber*f=(sp_Fiber*)sp_gc_alloc(sizeof(sp_Fiber),sp_Fiber_fin,sp_Fiber_scan);{size_t _g=sp_fiber_guard();f->stack=(char*)mmap(NULL,_g+SP_FIBER_STACK_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK,-1,0);if(f->stack==MAP_FAILED){f->stack=NULL;sp_raise_cls("FiberError","failed to allocate fiber stack");}mprotect(f->stack,_g,PROT_NONE);}f->state=0;f->transferred=0;f->body=body;f->yielded_value=sp_box_nil();f->resumed_value=sp_box_nil();f->user_data=NULL;f->saved_exc_top=0;f->saved_catch_top=0;f->exc_ctx=sp_exc_ctx_new();f->raised=0;f->raised_cls=NULL;f->raised_msg=NULL;f->raised_obj=NULL;f->inject=0;f->inj_cls=NULL;f->inj_msg=NULL;f->inj_obj=NULL;f->storage=NULL;f->saved_roots=NULL;f->saved_nroots=0;f->saved_roots_cap=0;f->fiber_next=NULL;f->fiber_prev=NULL;sp_fiber_list_add(f);sp_fiber_install_gc_hook();if(sp_fiber_current&&sp_fiber_current->storage){sp_Fiber*volatile _froot=f;int _pushed=0;if(sp_gc_nroots<SP_GC_STACK_MAX){sp_gc_roots[sp_gc_nroots++]=(void**)&_froot;_pushed=1;}f->storage=sp_FiberStore_dup((sp_FiberStore*)sp_fiber_current->storage);if(_pushed)sp_gc_nroots--;}
#ifdef SP_TSAN
  if(!sp_fiber_root.tsan_fiber)sp_fiber_root.tsan_fiber=__tsan_get_current_fiber();
  f->tsan_fiber=__tsan_create_fiber(0);f->caller_fiber=NULL;
#endif
  return f;}
sp_RbVal sp_Fiber_storage_get(sp_Fiber*f,sp_sym k){if(!f->storage)return sp_box_nil();return sp_FiberStore_get((sp_FiberStore*)f->storage,k);}
void sp_Fiber_storage_set(sp_Fiber*f,sp_sym k,sp_RbVal v){if(!f->storage)f->storage=sp_FiberStore_new();sp_FiberStore_set((sp_FiberStore*)f->storage,k,v);}
/* Internal class name of the Fiber#kill signal. It is raised to unwind the
   fiber (running ensure blocks) but is excluded from every user rescue clause by
   the codegen (emit_begin), so only ensures run; the trampoline below recognizes
   it and terminates without propagating. Must stay in sync with the literal the
   codegen emits in src/codegen_stmt.c. */
#define SP_FIBER_KILL_CLS "FiberKillSignal"

/* The inject slot is written by another OS thread (Thread#kill/#raise under the
   scheduler lock) but read lock-free by the target's own paths (the trampoline,
   Fiber.yield, sp_fiber_fire_inject_if_pending after a park). The flag alone is
   not enough: a second inject can arrive while the target is mid-consume, so the
   payload fields (inj_cls/inj_msg/inj_obj) would race with the consumer's reads
   and clears. Guard the whole slot with a spinlock folded into the high bit of
   the inject int: publishers and the consumer CAS the bit in, touch the payload,
   and release-store the new kind (which also drops the bit). The critical
   section is a handful of plain stores, so contention is a few spins at worst;
   the lock holder never takes the scheduler lock, so there is no ordering cycle
   with publishers that hold it. In the single-threaded build the atomics
   compile to plain ops on a never-contended slot. */
#define SP_INJECT_LOCK_BIT 0x40000000
#define SP_INJECT_PEEK(f) (__atomic_load_n(&(f)->inject, __ATOMIC_ACQUIRE) & ~SP_INJECT_LOCK_BIT)
/* Acquire the slot; returns the kind bits observed at lock time. */
static int sp_fiber_inject_lock(sp_Fiber*f){
  for(;;){
    int cur=__atomic_load_n(&f->inject,__ATOMIC_RELAXED);
    if(cur&SP_INJECT_LOCK_BIT)continue;   /* holder is mid-publish/consume; spin */
    if(__atomic_compare_exchange_n(&f->inject,&cur,cur|SP_INJECT_LOCK_BIT,0,
                                   __ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return cur;
  }
}
static void sp_fiber_inject_unlock(sp_Fiber*f,int kind){__atomic_store_n(&f->inject,kind,__ATOMIC_RELEASE);}
/* Publish kind+payload atomically with respect to a concurrent consume. */
static void sp_fiber_inject_publish(sp_Fiber*f,int kind,const char*cls,const char*msg,void*obj){
  sp_fiber_inject_lock(f);
  f->inj_cls=cls;f->inj_msg=msg;f->inj_obj=obj;
  sp_fiber_inject_unlock(f,kind);
}

/* Raise the exception queued by sp_Fiber_raise / sp_Fiber_kill in the fiber's
   own context. Runs at the fiber's suspension point (sp_Fiber_yield) or, for an
   unstarted fiber, at body entry (the trampoline). inject==2 is a kill signal;
   inject==1 is an ordinary raise. Clears the slot (under the inject spinlock)
   first so a rescue/retry does not re-fire it. */
static void sp_fiber_consume_inject(sp_Fiber*f){int kind=sp_fiber_inject_lock(f);const char*cl=f->inj_cls;const char*ms=f->inj_msg;void*ob=f->inj_obj;f->inj_cls=NULL;f->inj_msg=NULL;f->inj_obj=NULL;sp_fiber_inject_unlock(f,0);if(kind==2)sp_raise_cls(SP_FIBER_KILL_CLS,(&("\xff")[1]));else sp_fiber_reraise(cl,ms,ob);/* kind 1 (Fiber#raise) and 3 (Thread#raise) both re-raise */}

/* Thread #kill / #raise support: the scheduler sets a pending inject on a target
   thread's fiber, then fires it (sp_fiber_fire_inject_if_pending) when that
   thread next runs -- in its own context, so its ensure blocks unwind on its own
   stack. sp_fiber_raise_kill_self raises the kill signal in the running fiber
   (a thread killing itself).
   A thread raise is inject==3, NOT 1: the trampoline must not consume it at
   body entry. A Thread#raise that lands before the body has run (the target
   was picked up but not yet started) would otherwise raise ahead of the body's
   own begin/rescue and escape as an unhandled thread exception; deferring it to
   the thread's next suspension point (sp_sched_block / sleep / Thread.pass /
   yield) delivers it inside the body, where its rescue/ensure can see it. */
void sp_fiber_set_raise_inject(sp_Fiber*f,const char*cls,const char*msg,void*obj){sp_fiber_inject_publish(f,3,cls,msg,obj);}
void sp_fiber_set_kill_inject(sp_Fiber*f){sp_fiber_inject_publish(f,2,NULL,NULL,NULL);}
void sp_fiber_fire_inject_if_pending(void){sp_Fiber*f=sp_fiber_current;if(f&&SP_INJECT_PEEK(f))sp_fiber_consume_inject(f);}
/* Lock-free peek for the scheduler's pre-park checks (sp_sched_block etc.). */
int sp_fiber_inject_pending(sp_Fiber*f){return SP_INJECT_PEEK(f)!=0;}
SP_NORETURN void sp_fiber_raise_kill_self(void){sp_raise_cls(SP_FIBER_KILL_CLS,(&("\xff")[1]));}
static void sp_fiber_trampoline(void){sp_Fiber*f=sp_fiber_current;jmp_buf base;if(setjmp(base)==0){sp_exc_arm(base);{int _inj=SP_INJECT_PEEK(f);if(_inj&&_inj!=3)sp_fiber_consume_inject(f);}/* a thread raise (3) defers to the body's first suspension point */f->body(f);sp_exc_disarm();}else{const char*_cc=sp_exc_cur_cls();if(_cc&&!strcmp(_cc,SP_FIBER_KILL_CLS)){/* killed: ensures already ran while unwinding; terminate without propagating */}else{f->raised=1;f->raised_cls=_cc;f->raised_msg=sp_exc_cur_msg();f->raised_obj=sp_exc_cur_obj();}}f->state=3;f->saved_nroots=0;/* dead: the snapshot points into unwound frames; never mark it */if(f->transferred){sp_fiber_current=&sp_fiber_root;SP_TSAN_SWITCH(&sp_fiber_root);sp_ctx_swap(&f->ctx,&sp_fiber_root.ctx);}else{SP_TSAN_SWITCH(f->caller_fiber);sp_ctx_swap(&f->ctx,&f->caller_ctx);}}
sp_RbVal sp_Fiber_resume(sp_Fiber*f,sp_RbVal val){if(f->state==3){sp_raise_cls("FiberError","attempt to resume a terminated fiber");}if(f->transferred){sp_raise_cls("FiberError","attempt to resume a transferred fiber");}if(f->state==1){sp_raise_cls("FiberError","attempt to resume a resumed fiber (double resume)");}f->resumed_value=val;sp_Fiber*prev=sp_fiber_current;sp_fiber_save_roots(prev);sp_fiber_restore_roots(f);if(!prev->exc_ctx)prev->exc_ctx=sp_exc_ctx_new();sp_exc_ctx_save(prev->exc_ctx);sp_exc_ctx_load(f->exc_ctx);sp_fiber_current=f;SP_TSAN_SET_CALLER(f,prev);SP_TSAN_SWITCH(f);if(f->state==0){f->state=1;sp_ctx_make(&f->ctx,f->stack+sp_fiber_guard(),SP_FIBER_STACK_SIZE,sp_fiber_trampoline);sp_ctx_swap(&f->caller_ctx,&f->ctx);}else{f->state=1;sp_ctx_swap(&f->caller_ctx,&f->ctx);}sp_exc_ctx_save(f->exc_ctx);sp_exc_ctx_load(prev->exc_ctx);if(f->state!=3)sp_fiber_save_roots(f);sp_fiber_restore_roots(prev);sp_fiber_current=prev;if(f->raised){f->raised=0;const char*rc=f->raised_cls;const char*rm=f->raised_msg;void*ro=f->raised_obj;f->raised_obj=NULL;sp_fiber_reraise(rc,rm,ro);}return f->yielded_value;}
/* Fiber.yield is only valid inside a fiber entered via #resume. The root fiber
   was never resumed, and a fiber entered via #transfer has no resumer to return
   to (its caller_ctx is unset) -- yielding from either would swap to a garbage
   context, so raise instead (matching CRuby's FiberError). */
sp_RbVal sp_Fiber_yield(sp_RbVal val){sp_Fiber*f=sp_fiber_current;if(f==&sp_fiber_root||f->transferred){sp_raise_cls("FiberError","attempt to yield on a not resumed fiber");}f->yielded_value=val;f->state=2;SP_TSAN_SWITCH(f->caller_fiber);sp_ctx_swap(&f->ctx,&f->caller_ctx);if(SP_INJECT_PEEK(f))sp_fiber_consume_inject(f);return f->resumed_value;}
mrb_bool sp_Fiber_alive(sp_Fiber*f){return f->state!=3;}
/* Fiber#raise: queue an exception, then resume the fiber so its suspension point
   (or body entry) raises it. An unhandled raise propagates to this caller via
   sp_Fiber_resume's re-raise, exactly like an exception raised by the body. */
sp_RbVal sp_Fiber_raise(sp_Fiber*f,const char*cls,const char*msg,void*obj){
  if(f->state==3){sp_raise_cls("FiberError","dead fiber called");}
  sp_fiber_inject_publish(f,1,cls,msg,obj);   /* same-thread, but keep the slot discipline uniform */
  return sp_Fiber_resume(f,sp_box_nil());
}
/* Fiber#kill: terminate the fiber, running its ensure blocks. A suspended fiber
   is resumed with a kill signal that unwinds it (ensures run, user rescues are
   bypassed) until the trampoline terminates it. An unstarted fiber never ran its
   body, so it is just marked dead. Returns the fiber, matching CRuby. */
sp_Fiber*sp_Fiber_kill(sp_Fiber*f){
  if(f->state==3)return f;             /* already dead: no-op */
  if(f->state==0){f->state=3;return f;}/* unstarted: nothing to unwind */
  sp_fiber_inject_publish(f,2,NULL,NULL,NULL);
  sp_Fiber_resume(f,sp_box_nil());     /* runs ensures; the trampoline terminates it */
  return f;
}
/* Core of #transfer: switch to f and do all the save/restore, but leave f's
   unhandled termination exception PENDING in f->raised for the caller to
   consume. sp_Fiber_transfer re-raises it (Fiber semantics); the thread
   scheduler captures it instead (sp_Fiber_transfer_catch). */
static sp_RbVal sp_Fiber_transfer_core(sp_Fiber*f,sp_RbVal val){f->resumed_value=val;sp_Fiber*prev=sp_fiber_current;sp_fiber_save_roots(prev);sp_fiber_restore_roots(f);if(!prev->exc_ctx)prev->exc_ctx=sp_exc_ctx_new();sp_exc_ctx_save(prev->exc_ctx);sp_exc_ctx_load(f->exc_ctx);sp_fiber_current=f;SP_TSAN_SET_CALLER(f,prev);SP_TSAN_SWITCH(f);if(f->state==0&&f!=&sp_fiber_root){f->state=1;f->transferred=1;sp_ctx_make(&f->ctx,f->stack+sp_fiber_guard(),SP_FIBER_STACK_SIZE,sp_fiber_trampoline);sp_ctx_swap(&prev->ctx,&f->ctx);}else{/* the root fiber is the implicit running coroutine: it has no mmap'd
   stack/body, so it must never be ctx_make'd. Its context was already
   saved into root.ctx by the first transfer away from it, so transferring
   back just swaps to that saved context. */f->state=1;sp_ctx_swap(&prev->ctx,&f->ctx);}/* Do NOT re-save f's exception context or roots here. When f yielded back it
   already saved its own (its transfer's pre-swap save); the live TLS now holds
   prev's, which f restored before switching to us. Re-saving would clobber f's
   saved state with prev's -- losing a blocked/sleeping thread's roots/handlers
   and freeing its live locals on the next collection -- and, since f may already
   be running on another worker (woken between its switch-out and here), that
   write races that worker's load of f's context. We only restore prev's. */sp_exc_ctx_load(prev->exc_ctx);sp_fiber_restore_roots(prev);sp_fiber_current=prev;return prev->resumed_value;}
sp_RbVal sp_Fiber_transfer(sp_Fiber*f,sp_RbVal val){sp_RbVal r=sp_Fiber_transfer_core(f,val);if(f->raised){f->raised=0;const char*rc=f->raised_cls;const char*rm=f->raised_msg;void*ro=f->raised_obj;f->raised_obj=NULL;sp_fiber_reraise(rc,rm,ro);}return r;}
/* Thread scheduler transfer: on f's unhandled termination exception, hand the
   (cls,msg,obj) back through *out_* and set *out_raised, rather than re-raising
   in the caller (the scheduler stores it on the green thread for #join/#value).
   A non-terminating transfer (f yielded back) leaves *out_raised 0. */
sp_RbVal sp_Fiber_transfer_catch(sp_Fiber*f,sp_RbVal val,int*out_raised,const char**out_cls,const char**out_msg,void**out_obj){sp_RbVal r=sp_Fiber_transfer_core(f,val);*out_raised=f->raised;if(f->raised){f->raised=0;*out_cls=f->raised_cls;*out_msg=f->raised_msg;*out_obj=f->raised_obj;f->raised_obj=NULL;}return r;}

void sp_mark_fiber_root_storage(void){if(sp_fiber_root.storage)sp_gc_mark(sp_fiber_root.storage);}
