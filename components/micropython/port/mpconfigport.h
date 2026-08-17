/**
 * MicroPython build configuration for fmruby-core.
 *
 * This file is the only place where MicroPython feature selection happens.
 * It is read both when generating the embed package ("rake micropython:gen")
 * and when compiling the generated sources, so any change here needs a
 * regeneration before the next firmware build.
 */

#include <port/mpconfigport_common.h>

// Baseline feature set. CORE gives the language plus the small builtin
// modules that live in py/ (array, collections, math, struct, io, gc).
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

// Single precision: the firmware has no FPU-backed double path worth paying
// for, and float keeps object sizes down on the 32-bit targets.
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_FLOAT)

// Unwinding via setjmp/longjmp rather than the per-architecture assembler
// implementations, so linux (x86_64), Xtensa (S3) and RISC-V (P4) all behave
// the same way.
#define MICROPY_NLR_SETJMP                      (1)

// Same reasoning for the GC's register capture: setjmp spills the callee-saved
// registers to the jmp_buf, which the collector then scans as roots. Xtensa has
// no hand-written variant of that helper at all -- the generic one stops at an
// #error naming this setting -- so the choice is between setjmp everywhere and
// setjmp on some targets only.
#define MICROPY_GCREGS_SETJMP                   (1)

// Apps are shipped as .py source, so the compiler is mandatory.
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_PY_GC                           (1)

// No REPL: apps are files started by the launcher, never typed in.
#define MICROPY_HELPER_REPL                     (0)
#define MICROPY_REPL_AUTO_INDENT                (0)
#define MICROPY_REPL_EMACS_KEYS                 (0)

// No threads: the OS owns task creation, a guest VM does not get to spawn any.
#define MICROPY_PY_THREAD                       (0)

// Asynchronous abort, used to unwind the VM when the kernel asks the app to
// stop. The hook below polls for the request; mp_sched_vm_abort() then unwinds
// to the nlr buffer fmrb_mp_exec registered with nlr_set_abort().
//
// The hook body must stay free of fmruby headers: this file is preprocessed
// during qstr extraction too, where those include paths do not exist. Hence a
// bare declaration, with the implementation in fmrb_mp.c.
#define MICROPY_ENABLE_VM_ABORT                 (1)
extern void fmrb_mp_vm_hook(void);
#define MICROPY_VM_HOOK_LOOP                    fmrb_mp_vm_hook();

// Guard the C stack: parse and compile recurse, and an app task stack is far
// smaller than a desktop one. Overflow must raise RuntimeError, not corrupt
// the neighbouring task.
#define MICROPY_STACK_CHECK                     (1)

// import resolves built-in modules first, then .py files on sys.path. The two
// hooks that needs -- mp_import_stat and mp_reader_new_file -- are provided by
// modules/fmrb_module.c on top of the firmware's file layer, which is why
// neither of the stock readers is enabled: the POSIX one reads 20 bytes per
// call, and over flash that turns one import into hundreds of reads.
//
// sys.path is narrowed at app start (fmrb_mp.c) to the app's own directory and
// the shared library directory, so a guest cannot import from anywhere else.
// mp_builtin_open (mpport.c) still raises: reading a file as data goes through
// _fmrb.read_file, which has a size limit.
#define MICROPY_ENABLE_EXTERNAL_IMPORT          (1)
#define MICROPY_READER_POSIX                    (0)
#define MICROPY_READER_VFS                      (0)
#define MICROPY_HAS_FILE_READER                 (1)
#define MICROPY_PY_BUILTINS_INPUT               (0)

// Modules whose implementation lives in extmod/ are not part of the embed
// package by default, so they stay off unless they are pulled in on purpose
// (extmods/micropython.mk + the copy step in port/Makefile). Waiting is
// provided by the fmrb app module, so time stays out.
#define MICROPY_PY_TIME                         (0)

// random: pulled in because a game without it has to invent one. It depends on
// nothing but py/, and the extra functions (randint, randrange, choice,
// uniform) are what an app actually reaches for.
#define MICROPY_PY_RANDOM                       (1)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS           (1)
// Seeded on import from the millisecond clock, so two runs of the same app
// differ. The guest can still call random.seed(n) for a repeatable run.
#define MICROPY_MODULE_BUILTIN_INIT             (1)
extern unsigned long fmrb_mp_random_seed(void);
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC        (fmrb_mp_random_seed())

#define MICROPY_PY_SYS_PLATFORM                 "fmruby"
