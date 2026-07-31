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
// stop. Paired with a VM loop hook that polls the stop flag.
#define MICROPY_ENABLE_VM_ABORT                 (1)

// Guard the C stack: parse and compile recurse, and an app task stack is far
// smaller than a desktop one. Overflow must raise RuntimeError, not corrupt
// the neighbouring task.
#define MICROPY_STACK_CHECK                     (1)

// import only resolves to built-in modules. There is no filesystem importer
// wired up (mp_import_stat / mp_builtin_open are not provided).
#define MICROPY_ENABLE_EXTERNAL_IMPORT          (0)
#define MICROPY_READER_POSIX                    (0)
#define MICROPY_READER_VFS                      (0)
#define MICROPY_PY_BUILTINS_INPUT               (0)

// Modules whose implementation lives in extmod/ are not part of the embed
// package, so they stay off. Waiting is provided by the fmrb app module.
#define MICROPY_PY_TIME                         (0)

#define MICROPY_PY_SYS_PLATFORM                 "fmruby"
