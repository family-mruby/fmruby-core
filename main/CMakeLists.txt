set(COMPONENTS main)
set(COMPONENT_SRCS
    "main.c"
    "boot/boot.c"
    "app/fmrb_app.c"
    "app/fmrb_app_spawner.c"
    "kernel/fmrb_kernel.c"
    "kernel/host/host_task.c"
    "drivers/fs_proxy/fs_proxy_task.c"
    "drivers/usb/fmrb_keymap.c"
)
set(COMPONENT_LINUX_SRCS
    "drivers/usb/usb_task_linux.c"
)
set(COMPONENT_ESP32_SRCS
    "drivers/usb/usb_task.c"
)

# Include Ruby to bytecode compilation script
include(${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/compile_ruby_to_bytecode.cmake)

# Find all Ruby files in prebuild_scripts/kernel/
file(GLOB KERNEL_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/kernel/*.rb)

# Find all Ruby files in prebuild_scripts/default_app/
file(GLOB DEFAULT_APP_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/default_app/*.rb)

# Step 1: Prepare Ruby bytecode sources (add C files to COMPONENT_SRCS)
prepare_ruby_bytecode_sources(KERNEL_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/kernel/mrb COMPONENT_SRCS)
prepare_ruby_bytecode_sources(DEFAULT_APP_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/default_app/mrb COMPONENT_SRCS)

# Set up component dependencies based on target
set(COMPONENT_REQUIRES fmrb_hal fmrb_log fmrb_msg fmrb_link fmrb_toml fmrb_mem
    fmrb_gfx fmrb_audio lua
    picoruby-esp32 mem_allocator log tar_lib)

# Add ESP32-specific components
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND COMPONENT_REQUIRES esp_littlefs)
    list(APPEND COMPONENT_SRCS ${COMPONENT_ESP32_SRCS})
else()
    list(APPEND COMPONENT_SRCS ${COMPONENT_LINUX_SRCS})
endif()

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "include" "boot" "kernel" "kernel/host" "drivers/fs_proxy" "drivers/usb"
    REQUIRES ${COMPONENT_REQUIRES}
)

# Disable optimization for app.c to enable proper debugging
# Todo: disable it on official release
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-O0 -g3>
)

# Step 2: Generate Ruby bytecode compilation commands (must be after idf_component_register)
generate_ruby_bytecode_commands(KERNEL_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/kernel/mrb)
generate_ruby_bytecode_commands(DEFAULT_APP_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/default_app/mrb)