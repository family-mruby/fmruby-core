# WAMR proof of concept host. Built twice from the same sources:
#   cmake  -S host -B build/linux                       (Linux x86-64)
#   emcmake cmake -S host -B build/emscripten           (node)
cmake_minimum_required(VERSION 3.14)
project(wamr_poc_host C)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif ()

if (NOT DEFINED WAMR_ROOT_DIR)
  set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../vendor/wamr-2.4.5)
endif ()

# Our own platform layer, not WAMR's linux one.
set(WAMR_BUILD_PLATFORM "fmrb_poc")
set(SHARED_PLATFORM_CONFIG ${CMAKE_CURRENT_LIST_DIR}/platform/shared_platform.cmake)

if (EMSCRIPTEN)
  # wasm32: 32-bit little endian.
  set(WAMR_BUILD_TARGET "X86_32")
else ()
  set(WAMR_BUILD_TARGET "X86_64")
endif ()
# invokeNative (typed host calls) is removed below and replaced by a trap;
# selecting the C version only keeps the assembler out of the build.
set(WAMR_BUILD_INVOKE_NATIVE_GENERAL 1)

# The fixed feature set: classic interpreter only.
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
if (NOT DEFINED WAMR_BUILD_BULK_MEMORY)
  set(WAMR_BUILD_BULK_MEMORY 1)
endif ()
# Rust (1.82+) and Zig emit the overlong call_indirect encoding that WAMR
# only accepts with reference types; Zig's default CPU (lime1) also allows
# extended constant expressions. Both on, so the compilers' defaults load.
# run.rb's "features" step builds a variant with both off.
if (NOT DEFINED WAMR_BUILD_REF_TYPES)
  set(WAMR_BUILD_REF_TYPES 1)
endif ()
if (NOT DEFINED WAMR_BUILD_EXTENDED_CONST_EXPR)
  set(WAMR_BUILD_EXTENDED_CONST_EXPR 1)
endif ()

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

list(FILTER WAMR_RUNTIME_LIB_SOURCE EXCLUDE REGEX "invokeNative_")
add_library(vmlib STATIC ${WAMR_RUNTIME_LIB_SOURCE}
  ${CMAKE_CURRENT_LIST_DIR}/platform/poc_no_invoke_native.c)

add_executable(poc_host host.c)
target_link_libraries(poc_host vmlib)
# The same program shape without WAMR, to measure what WAMR adds.
add_executable(baseline baseline.c)
foreach (t poc_host baseline)
  if (EMSCRIPTEN)
    # NODERAWFS: fopen reads the host file system (node only).
    target_link_options(${t} PRIVATE
      -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1
      -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
  else ()
    target_link_libraries(${t} m)
  endif ()
endforeach ()
