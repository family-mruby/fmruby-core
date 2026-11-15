# The WAMR platform layer of Family mruby (SHARED_PLATFORM_CONFIG of
# runtime_lib.cmake). One layer for every build: ESP32-P4, the Linux
# simulation and the hosted builds (native, browser), all on FreeRTOS and
# fmrb_hal / fmrb_mem.
set (PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions(-DBH_PLATFORM_FMRB)

include_directories(${PLATFORM_SHARED_DIR})
include_directories(${WAMR_ROOT_DIR}/core/shared/platform/include)

set (PLATFORM_SHARED_SOURCE
  ${PLATFORM_SHARED_DIR}/fmrb_wamr_platform.c
  ${PLATFORM_SHARED_DIR}/fmrb_wamr_no_invoke_native.c)
