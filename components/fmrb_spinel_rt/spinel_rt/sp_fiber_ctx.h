/* sp_fiber_ctx.h -- portable coroutine context switch for sp_fiber.c.
 *
 * Replaces POSIX <ucontext.h> (getcontext/makecontext/swapcontext), which
 * OpenBSD removed in 5.7 (W^X / ASLR hardening) and which is deprecated on
 * macOS. On x86_64 and aarch64 we use a tiny cooperative register switch
 * written as file-scope inline asm (defined in sp_fiber.c): it lands in .text
 * (W^X-safe, unlike libco's executable data blob), does no syscall (unlike
 * swapcontext's sigprocmask, so it's faster), and only saves the callee-saved
 * registers + stack pointer. The "context" is therefore just the saved stack
 * pointer; all other state lives on the coroutine's own stack. Other
 * architectures fall back to <ucontext.h>.
 *
 *   sp_ctx_swap(from, to)               save the current context into *from,
 *                                       then switch to *to.
 *   sp_ctx_make(ctx, base, size, entry) prime a fresh context so the first swap
 *                                       into it begins executing entry() on the
 *                                       stack [base, base+size). entry must
 *                                       never return.
 */
#ifndef SP_FIBER_CTX_H
#define SP_FIBER_CTX_H

#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__) || defined(__aarch64__)
  #define SP_FIBER_ASM 1
#else
  #define SP_FIBER_ASM 0
#endif

#if SP_FIBER_ASM

/* The whole machine context is the saved stack pointer; the callee-saved
   registers are pushed onto / popped from the coroutine's own stack by
   sp_ctx_swap (see the file-scope asm in sp_fiber.c). */
typedef struct sp_fiber_ctx { void *sp; } sp_fiber_ctx;

static inline void sp_ctx_make(sp_fiber_ctx *ctx, void *base, size_t size,
                               void (*entry)(void)) {
  uintptr_t top = ((uintptr_t)base + size) & ~(uintptr_t)15; /* 16B-align the top */
#if defined(__x86_64__)
  /* SysV AMD64: entry's first instruction needs rsp%16 == 8 (as if reached by
     `call`). sp_ctx_swap pops 6 callee-saved regs then `ret`s into the slot
     just above them: [entry][rbp][rbx][r12][r13][r14][r15<-sp]. */
  void **s = (void **)(top - 8);
  *--s = (void *)entry;                  /* `ret` target, at top-16 */
  for (int i = 0; i < 6; i++) *--s = 0;  /* rbp, rbx, r12, r13, r14, r15 */
  ctx->sp = s;                           /* top-64 */
#else /* __aarch64__ */
  /* Mirror the aarch64 sp_ctx_swap frame: 160 bytes holding x19..x30 (12) then
     d8..d15 (8). x30 (lr = entry) sits at offset 88 (slot 11). On the first
     swap the ldp's load zeros + entry into lr and `ret` (br x30) jumps to it. */
  void **s = (void **)(top - 160);
  for (int i = 0; i < 20; i++) s[i] = 0;
  s[11] = (void *)entry;                 /* x30 (lr) */
  ctx->sp = s;
#endif
}

#else  /* portable fallback: POSIX ucontext */

#include <ucontext.h>
typedef struct sp_fiber_ctx { ucontext_t uc; } sp_fiber_ctx;

static inline void sp_ctx_make(sp_fiber_ctx *ctx, void *base, size_t size,
                               void (*entry)(void)) {
  getcontext(&ctx->uc);
  ctx->uc.uc_stack.ss_sp = base;
  ctx->uc.uc_stack.ss_size = size;
  ctx->uc.uc_link = NULL;               /* entry never returns (trampoline switches) */
  makecontext(&ctx->uc, entry, 0);
}

#endif

/* Defined as file-scope asm in sp_fiber.c (asm backend) or a swapcontext
   wrapper (fallback). */
void sp_ctx_swap(sp_fiber_ctx *from, sp_fiber_ctx *to);

#endif /* SP_FIBER_CTX_H */
