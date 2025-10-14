cmake_minimum_required(VERSION 3.16)
project(fmrb_host_sdl2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_definitions(-DLGFX_SDL)

# Find SDL2 and msgpack
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(MSGPACK REQUIRED msgpack)

# LovyanGFX path
set(LOVYANGFX_PATH ../LovyanGFX)

# Include directories
include_directories(
    include
    ../common
    ${SDL2_INCLUDE_DIRS}
    ${MSGPACK_INCLUDE_DIRS}
    ${LOVYANGFX_PATH}/src
)

# LovyanGFX source files
file(GLOB LOVYANGFX_SOURCES
    ${LOVYANGFX_PATH}/src/lgfx/Fonts/efont/*.c
    ${LOVYANGFX_PATH}/src/lgfx/Fonts/IPA/*.c
    ${LOVYANGFX_PATH}/src/lgfx/utility/*.c
    ${LOVYANGFX_PATH}/src/lgfx/v1/*.cpp
    ${LOVYANGFX_PATH}/src/lgfx/v1/misc/*.cpp
    ${LOVYANGFX_PATH}/src/lgfx/v1/panel/Panel_Device.cpp
    ${LOVYANGFX_PATH}/src/lgfx/v1/panel/Panel_FrameBufferBase.cpp
    ${LOVYANGFX_PATH}/src/lgfx/v1/platforms/sdl/*.cpp
)

# Source files
set(SOURCES
    src/main.cpp
    src/graphics_handler.cpp
    src/audio_handler.c
    src/socket_server.c
    ../common/fmrb_ipc_cobs.c
    ${LOVYANGFX_SOURCES}
)

# Create executable
add_executable(fmrb_host_sdl2 ${SOURCES})

# Link libraries
target_link_libraries(fmrb_host_sdl2
    ${SDL2_LIBRARIES}
    ${MSGPACK_LIBRARIES}
    pthread
    m
)

# Compiler flags
target_compile_options(fmrb_host_sdl2 PRIVATE ${SDL2_CFLAGS_OTHER})

# Install target
install(TARGETS fmrb_host_sdl2
    RUNTIME DESTINATION bin
)

# Print build information
message(STATUS "Building fmrb_host_sdl2 with SDL2 and LovyanGFX")
message(STATUS "SDL2 libraries: ${SDL2_LIBRARIES}")
message(STATUS "SDL2 include dirs: ${SDL2_INCLUDE_DIRS}")
