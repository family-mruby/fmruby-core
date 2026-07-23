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
    ${SPINEL_MC_FLAGS}
)

# Ensure the final image links with --gc-sections so uncalled runtime functions
# (and their references into non-compiled modules) are dropped. INTERFACE so any
# component/executable that links this one inherits it.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--gc-sections")

# Expose the chosen SP_GC_STACK_MAX to dependents (the generated-C compile).
target_compile_definitions(${COMPONENT_LIB} INTERFACE "SP_GC_STACK_MAX=${SPINEL_RT_GC_STACK_MAX}")

# NOTE: the multi-instance flags are deliberately NOT propagated INTERFACE.
# -include sp_mem_override.h remaps malloc/free, which must apply ONLY to the
# Spinel runtime (here, PRIVATE) and the generated Spinel program TU -- NOT to
# the rest of the consuming component (host_task.c, fmrb_app.c, the fmrb_spx
# FFI shims), whose C uses libc / fmrb_mem and would break if rerouted through
# an instance pool. The generated C gets these flags per-file in
# main/CMakeLists.txt. This path is exported so that site stays in sync:
set(SPINEL_MC_FLAGS "${SPINEL_MC_FLAGS}" CACHE INTERNAL "SP_MULTI_CTX flags for the generated Spinel C")
