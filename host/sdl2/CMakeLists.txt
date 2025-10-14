cmake_minimum_required(VERSION 3.16)
project(fmrb_host_sdl2)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Include directories
include_directories(
    include
    ../common
    ${SDL2_INCLUDE_DIRS}
)

# Source files
set(SOURCES
    src/main.c
    src/graphics_handler.c
    src/audio_handler.c
    src/socket_server.c
    ../common/fmrb_ipc_cobs.c
)

# Create executable
add_executable(fmrb_host_sdl2 ${SOURCES})

# Link libraries
target_link_libraries(fmrb_host_sdl2
    ${SDL2_LIBRARIES}
    pthread
)

# Compiler flags
target_compile_options(fmrb_host_sdl2 PRIVATE ${SDL2_CFLAGS_OTHER})

# Install target
install(TARGETS fmrb_host_sdl2
    RUNTIME DESTINATION bin
)

# Print build information
message(STATUS "Building fmrb_host_sdl2 with SDL2")
message(STATUS "SDL2 libraries: ${SDL2_LIBRARIES}")
message(STATUS "SDL2 include dirs: ${SDL2_INCLUDE_DIRS}")