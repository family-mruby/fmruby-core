# hosted/wamr - WAMR and the wasm apps on it, for the hosted builds (the
# native program and the browser build; doc/wasm_apps W1c).
#
# The same configuration as components/fmrb_wamr/CMakeLists.txt (the ESP-IDF
# component of the P4, the S3 and the Linux simulation), with its platform layer and
# fmrb_wasm.c. It is a directory of its own because WAMR's
# build-scripts/runtime_lib.cmake (and the platform's shared_platform.cmake)
# set include paths and definitions for the whole directory: here they stay
# out of the firmware's other targets. fmrb_wasm.c is compiled here because it
# reads the interpreter's own structures, which need those definitions.
#
# Added by hosted.cmake (add_subdirectory), after its source lists and
# FMRB_HOSTED_CORE_INCLUDES / _DEFINES are set. Gives the static library
# fmrb_wamr_hosted; the platform's executables link it.

set(FMRB_WAMR_COMPONENT ${FMRB_ROOT}/components/fmrb_wamr)
set(WAMR_ROOT_DIR ${FMRB_ROOT}/vendor/wamr)
if(NOT EXISTS ${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
  message(FATAL_ERROR "WAMR is not in vendor/wamr: run `rake wamr:setup` (rake setup does)")
endif()

set(WAMR_BUILD_PLATFORM "fmrb")
set(SHARED_PLATFORM_CONFIG ${FMRB_WAMR_COMPONENT}/platform/shared_platform.cmake)
if(FMRB_HOSTED_PLATFORM STREQUAL "wasm")
  # wasm32 is 32-bit little endian; WAMR has no emscripten target
  # (report/w1a.md 3).
  set(WAMR_BUILD_TARGET "X86_32")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(WAMR_BUILD_TARGET "AARCH64")
else()
  set(WAMR_BUILD_TARGET "X86_64")
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
# invokeNative is replaced by a trap (every host function is registered raw).
list(FILTER WAMR_RUNTIME_LIB_SOURCE EXCLUDE REGEX "invokeNative_")

add_library(fmrb_wamr_hosted STATIC ${WAMR_RUNTIME_LIB_SOURCE} ${FMRB_WAMR_COMPONENT}/fmrb_wasm.c)
target_include_directories(fmrb_wamr_hosted PRIVATE ${FMRB_HOSTED_CORE_INCLUDES})
target_include_directories(fmrb_wamr_hosted PUBLIC ${FMRB_WAMR_COMPONENT}/include)
target_compile_definitions(fmrb_wamr_hosted PRIVATE ${FMRB_HOSTED_CORE_DEFINES})
target_link_libraries(fmrb_wamr_hosted PUBLIC freertos_hosted)
# WAMR's own sources are not ours to clean up: their warnings stay warnings.
target_compile_options(fmrb_wamr_hosted PRIVATE -pthread -Wno-error -Wno-unused-variable
  -Wno-unused-function -Wno-format ${FMRB_HOSTED_SECTION_FLAGS})
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  target_compile_options(fmrb_wamr_hosted PRIVATE -Wno-maybe-uninitialized)
endif()
