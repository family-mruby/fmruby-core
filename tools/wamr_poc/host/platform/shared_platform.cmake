# Platform layer of the WAMR proof of concept (used through SHARED_PLATFORM_CONFIG).
set (PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions(-DBH_PLATFORM_FMRB_POC)

include_directories(${PLATFORM_SHARED_DIR})
include_directories(${WAMR_ROOT_DIR}/core/shared/platform/include)

set (PLATFORM_SHARED_SOURCE ${PLATFORM_SHARED_DIR}/poc_platform.c)
