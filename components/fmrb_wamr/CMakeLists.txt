# WAMR (wasm-micro-runtime) and the wasm apps on it (doc/wasm_apps W1b).
#
# Built on the ESP32-P4, the ESP32-S3 and the Linux simulation (an empty
# component elsewhere). WAMR's source is the pinned checkout in
# vendor/wamr (rake wamr:setup, lib/upstream/WAMR_PIN); the platform layer
# under platform/ is ours. The configuration is fixed here (decision 1):
# the classic interpreter only, reference types and extended constant
# expressions on, instruction metering on; no AOT, JIT, WASI, libc, threads,
# shared memory, multi-module or SIMD. invokeNative is replaced by a trap:
# every host function is registered raw.
if(NOT (IDF_TARGET STREQUAL "esp32p4" OR IDF_TARGET STREQUAL "esp32s3"
        OR IDF_TARGET STREQUAL "linux"))
    idf_component_register()
    return()
endif()

set(FMRB_WAMR_REQUIRES fmrb_common fmrb_hal fmrb_mem fmrb_log fmrb_gfx freertos)
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND FMRB_WAMR_REQUIRES heap)
endif()

if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(INCLUDE_DIRS "include" REQUIRES ${FMRB_WAMR_REQUIRES})
    return()
endif()

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../vendor/wamr)
if(NOT EXISTS ${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
    message(FATAL_ERROR "WAMR is not in vendor/wamr: run `rake wamr:setup` (rake setup does)")
endif()

set(WAMR_BUILD_PLATFORM "fmrb")
set(SHARED_PLATFORM_CONFIG ${CMAKE_CURRENT_LIST_DIR}/platform/shared_platform.cmake)
if(IDF_TARGET STREQUAL "linux")
    set(WAMR_BUILD_TARGET "X86_64")
elseif(IDF_TARGET STREQUAL "esp32s3")
    # Xtensa LX7: no unaligned loads, so WAMR's config.h turns
    # WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS off and the interpreter reads
    # the bytecode byte by byte.
    set(WAMR_BUILD_TARGET "XTENSA")
else()
    set(WAMR_BUILD_TARGET "RISCV32_ILP32F")
endif()
set(WAMR_BUILD_INVOKE_NATIVE_GENERAL 1)
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 0)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_THREAD_MGR 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_SHARED_MEMORY 0)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_INSTRUCTION_METERING 1)
set(WAMR_BUILD_BULK_MEMORY 1)
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_BUILD_EXTENDED_CONST_EXPR 1)

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
list(FILTER WAMR_RUNTIME_LIB_SOURCE EXCLUDE REGEX "invokeNative_")

set(FMRB_WAMR_LDFRAGMENTS "")
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND FMRB_WAMR_LDFRAGMENTS "fmrb_wamr_flash.lf")
endif()
if(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
    list(APPEND FMRB_WAMR_LDFRAGMENTS "fmrb_wamr_psram.lf")
endif()

idf_component_register(
    SRCS ${WAMR_RUNTIME_LIB_SOURCE} "fmrb_wasm.c"
    INCLUDE_DIRS "include"
    REQUIRES ${FMRB_WAMR_REQUIRES}
    LDFRAGMENTS ${FMRB_WAMR_LDFRAGMENTS}
)
# WAMR's own sources are not ours to clean up: their warnings stay warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-unused-variable
    -Wno-unused-function -Wno-format -Wno-maybe-uninitialized)
# No small-data sections on RISC-V: .sbss is not covered by the PSRAM
# mapping (fmrb_wamr_psram.lf), so small globals would land in internal RAM.
# (Xtensa has no small data: everything is .bss/.data already.)
if(IDF_TARGET STREQUAL "esp32p4")
    target_compile_options(${COMPONENT_LIB} PRIVATE -msmall-data-limit=0)
endif()
# Every user of the component knows wasm apps are there.
target_compile_definitions(${COMPONENT_LIB} INTERFACE FMRB_HAVE_WASM=1)
