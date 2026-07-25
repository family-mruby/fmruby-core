# Spinel runtime component (snapshot of the fork's lib/, imported by
# import_from_fork.rb). Provides the C runtime that Spinel-generated programs
# (kernel VM, later) link against. In Phase 1 it is built but unused until the
# hello_kernel entry (T1-6) references it.
#
# Only a minimal set of the runtime is compiled here (COMPILE_SRCS); the full
# header set lives in spinel_rt/ because sp_runtime.h transitively includes the
# excluded modules' headers. -ffunction-sections + -fdata-sections let the final
# link's --gc-sections drop any uncalled reference into an excluded module.
#
# SP_GC_STACK_MAX MUST match the value used to compile Spinel-generated C
# (see compile_ruby_to_spinel.cmake). sp_gc_roots is a fixed-size BSS array of
# this length; a mismatch across translation units is a silent ABI break.

set(SPINEL_RT_GC_STACK_MAX 8192 CACHE STRING "SP_GC_STACK_MAX for the Spinel runtime + generated C")

# Capacity of the runtime symbol-intern pool (sp_dyn_syms), a fixed BSS array the
# generated TU declares -- one pointer per symbol minted at run time by
# String#to_sym or :"#{...}". Upstream's 8192 costs 32 KB of internal DRAM per
# generated program (kernel + desktop = 64 KB), which is most of what this board
# has. The Ruby we compile interns two call sites' worth of theme keys and names,
# and sp_sym_intern deduplicates, so only distinct names ever consume a slot:
# a few dozen at the very most.
#
# Applied to Linux as well as the embedded targets, deliberately. Making this a
# port-only flag would leave the two builds with different symbol capacities,
# which is exactly the kind of divergence the dual build exists to rule out.
set(SPINEL_RT_DYN_SYMS_MAX 256 CACHE STRING "SP_DYN_SYMS_MAX for the Spinel runtime + generated C")

# Depth of the begin/rescue and catch/throw handler stacks, another pair of fixed
# BSS arrays in the generated TU. Each entry holds a jmp_buf -- 304 bytes on RV32 --
# so this reserves ~42 KB per generated program for the two together, and dropping
# it to 32 would hand back ~42 KB of internal DRAM across the kernel and desktop.
#
# It stays at the upstream default anyway, because 32 was tried and is NOT safe:
# opening and closing the desktop's config dialog died with a bogus
# "unhandled exception: out of memory". The desktop really does nest more than 32
# begin frames, and a begin-frame push is not bounds-checked (sp_rescue_push exits
# on overflow, the begin push does not), so the overflow wrote past sp_exc_stack
# into the neighbouring arrays -- sp_exc_rootmark among them, which is how the GC
# tracks live roots -- and surfaced as a fake allocation failure.
#
# To reclaim that memory, first make the push fail loudly (seven codegen emit sites
# in the fork, plus regenerating), then measure the real high-water mark and size
# this from the measurement. Guessing from the task stack does not work: a Ruby
# frame costs 2-6 KB, which suggested only a handful of frames could be live, and
# the measurement contradicted that.
#
# Kept identical on Linux so the dual build cannot diverge. That is what caught
# this -- the bad value failed in headless testing instead of on a device.
set(SPINEL_RT_EXC_STACK_MAX 64 CACHE STRING "SP_EXC_STACK_MAX / SP_CATCH_STACK_MAX for the generated C")

# Capacity of the GC's mark work list, allocated lazily as ONE contiguous block
# of this many pointers on the instance's own pool. Upstream's 64K entries is
# 256 KB on a 32-bit target -- half of the kernel's 512 KB pool -- so the first
# collection asked for a block the pool could not produce once it held any live
# data, and sp_mem_malloc turns that into sp_oom_die: the P4 rebooted from
# inside sp_gc_mark_all, both while idle and on the first window drag.
#
# Overflow is safe here, unlike the exception stacks above: sp_gc_mark pushes
# while there is room and otherwise recurses through the scan hook, so a value
# that is too small costs C stack and time, not correctness. 8192 entries is
# 32 KB -- above the object count a 512 KB pool can hold, so the recursive path
# stays rare, while the peak request drops from 256 KB to 32 KB.
set(SPINEL_RT_GC_MARK_STACK_MAX 8192 CACHE STRING "SP_GC_MARK_STACK_MAX for the Spinel runtime + generated C")

# Multi-instance (SP_MULTI_CTX): the runtime state lives in a per-instance sp_ctx
# and allocation routes through sp_mem_override.h. This lets kernel + desktop run
# as separate Spinel instances in one image, each on its own estalloc pool.
# BOTH the runtime snapshot AND the generated C (compile_ruby_to_spinel.cmake)
# MUST be compiled with the same two flags below -- a mismatch is a silent ABI
# break (like SP_GC_STACK_MAX). They are fed from this single variable: PRIVATE
# to the runtime, INTERFACE to every dependent (the generated-C component).
set(SPINEL_RT_MULTI_CTX ON CACHE BOOL "Build the Spinel runtime + generated C multi-instance (SP_MULTI_CTX)")
set(SPINEL_MC_FLAGS "")
if(SPINEL_RT_MULTI_CTX)
    set(SPINEL_MC_FLAGS -DSP_MULTI_CTX -include ${CMAKE_CURRENT_LIST_DIR}/spinel_rt/sp_mem_override.h)
endif()

# Compiled runtime = every .c in the snapshot (import_from_fork.rb decides the
# set: the whole runtime minus sp_net.c / sp_crypto.c). Glob so CMake stays in
# sync with the import without a hand-maintained list. fmrb_spinel_host.c (the
# host shim that creates an instance on an estalloc pool) lives OUTSIDE spinel_rt/
# so import_from_fork.rb does not wipe it.
file(GLOB COMPILE_SRCS ${CMAKE_CURRENT_LIST_DIR}/spinel_rt/*.c)

# Port flags for MMU-less targets (ESP32/newlib has no <sys/mman.h>). On such a
# target sp_runtime.h must not pull <sys/mman.h>, so define SP_NO_MMAN; and the
# coroutine pair -- sp_fiber.c (mmaps its stacks; #errors under SP_NO_MMAN) and
# sp_sched.c (its green-thread scheduler, needs <poll.h>) -- must be dropped from
# the build. The kernel/desktop never use fibers or threads, so --gc-sections
# removes the remaining (uncompiled) references. Linux (CONFIG_IDF_TARGET_LINUX)
# keeps them and the hosted headers unchanged. The flag must also reach the
# generated Spinel C (which includes sp_runtime.h); that site is
# main/CMakeLists.txt, kept in sync via SPINEL_PORT_FLAGS below.
# SP_STACK_SCRATCH_MAX bounds the runtime's on-stack scratch buffers to what a
# FreeRTOS task stack can carry (see the same flag in main/CMakeLists.txt, which
# must stay in sync -- the generated C compiles against these same headers).
set(SPINEL_PORT_FLAGS "")
if(NOT CONFIG_IDF_TARGET_LINUX)
    set(SPINEL_PORT_FLAGS -DSP_NO_MMAN -DSP_STACK_SCRATCH_MAX=512)
    list(REMOVE_ITEM COMPILE_SRCS
        "${CMAKE_CURRENT_LIST_DIR}/spinel_rt/sp_fiber.c"
        "${CMAKE_CURRENT_LIST_DIR}/spinel_rt/sp_sched.c")
endif()

idf_component_register(
    SRCS ${COMPILE_SRCS} "fmrb_spinel_host.c"
    INCLUDE_DIRS "spinel_rt" "include"
)

# The runtime is third-party C tuned with -Wno-all upstream; do not let the
# firmware's stricter/-Werror flags fail it. function/data-sections enable
# dead-code elimination of the excluded modules at final link.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -w
    -ffunction-sections
    -fdata-sections
    "-DSP_GC_STACK_MAX=${SPINEL_RT_GC_STACK_MAX}"
    "-DSP_DYN_SYMS_MAX=${SPINEL_RT_DYN_SYMS_MAX}"
    "-DSP_EXC_STACK_MAX=${SPINEL_RT_EXC_STACK_MAX}"
    "-DSP_CATCH_STACK_MAX=${SPINEL_RT_EXC_STACK_MAX}"
    "-DSP_GC_MARK_STACK_MAX=${SPINEL_RT_GC_MARK_STACK_MAX}"
    ${SPINEL_MC_FLAGS}
    ${SPINEL_PORT_FLAGS}
)

# Ensure the final image links with --gc-sections so uncalled runtime functions
# (and their references into non-compiled modules) are dropped. INTERFACE so any
# component/executable that links this one inherits it.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--gc-sections")

# Expose the chosen sizes to dependents (the generated-C compile). sp_gc_roots and
# sp_dyn_syms are both fixed BSS arrays declared by the generated TU, so these must
# match what the runtime was built with.
target_compile_definitions(${COMPONENT_LIB} INTERFACE "SP_GC_STACK_MAX=${SPINEL_RT_GC_STACK_MAX}")
target_compile_definitions(${COMPONENT_LIB} INTERFACE "SP_DYN_SYMS_MAX=${SPINEL_RT_DYN_SYMS_MAX}")
target_compile_definitions(${COMPONENT_LIB} INTERFACE "SP_EXC_STACK_MAX=${SPINEL_RT_EXC_STACK_MAX}")
target_compile_definitions(${COMPONENT_LIB} INTERFACE "SP_CATCH_STACK_MAX=${SPINEL_RT_EXC_STACK_MAX}")
# The collector allocates the work list, the generated TU's sp_gc_mark decides
# against the same bound whether to push or recurse: both must agree.
target_compile_definitions(${COMPONENT_LIB} INTERFACE "SP_GC_MARK_STACK_MAX=${SPINEL_RT_GC_MARK_STACK_MAX}")

# NOTE: the multi-instance flags are deliberately NOT propagated INTERFACE.
# -include sp_mem_override.h remaps malloc/free, which must apply ONLY to the
# Spinel runtime (here, PRIVATE) and the generated Spinel program TU -- NOT to
# the rest of the consuming component (host_task.c, fmrb_app.c, the fmrb_spx
# FFI shims), whose C uses libc / fmrb_mem and would break if rerouted through
# an instance pool. The generated C gets these flags per-file in
# main/CMakeLists.txt. This path is exported so that site stays in sync:
set(SPINEL_MC_FLAGS "${SPINEL_MC_FLAGS}" CACHE INTERNAL "SP_MULTI_CTX flags for the generated Spinel C")

# Port flags (SP_NO_MMAN on MMU-less targets) the generated Spinel C must also
# carry, since it includes sp_runtime.h. Applied per-file in main/CMakeLists.txt.
set(SPINEL_PORT_FLAGS "${SPINEL_PORT_FLAGS}" CACHE INTERNAL "Port flags for the generated Spinel C")
