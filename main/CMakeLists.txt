set(COMPONENTS main)
set(COMPONENT_SRCS
    "main.c"
    "boot.c"
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

# Collect all Ruby files to compile to bytecode
set(PICORBC ${CMAKE_CURRENT_LIST_DIR}/../components/picoruby-esp32/picoruby/bin/picorbc)
set(GENERATED_C_FILES "")

# Find all Ruby files in kernel/mrblib/
file(GLOB KERNEL_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/kernel/mrblib/*.rb)

# Find all Ruby files in app/default_app/mrblib/
file(GLOB DEFAULT_APP_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/app/default_app/mrblib/*.rb)

# Process kernel Ruby files
foreach(RB_FILE ${KERNEL_RUBY_FILES})
  get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
  set(C_FILE ${CMAKE_CURRENT_LIST_DIR}/kernel/mrb/${RB_NAME}.c)
  list(APPEND GENERATED_C_FILES ${C_FILE})
  list(APPEND COMPONENT_SRCS ${C_FILE})
endforeach()

# Process default_app Ruby files
foreach(RB_FILE ${DEFAULT_APP_RUBY_FILES})
  get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
  set(C_FILE ${CMAKE_CURRENT_LIST_DIR}/app/default_app/mrb/${RB_NAME}.c)
  list(APPEND GENERATED_C_FILES ${C_FILE})
  list(APPEND COMPONENT_SRCS ${C_FILE})
endforeach()

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
    INCLUDE_DIRS "." "kernel" "kernel/host" "include" "drivers/fs_proxy" "drivers/usb"
    REQUIRES ${COMPONENT_REQUIRES}
)

# Disable optimization for app.c to enable proper debugging
# Todo: disable it on official release
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-O0 -g3>
)

# Generate C files from kernel Ruby files (must be after idf_component_register)
foreach(RB_FILE ${KERNEL_RUBY_FILES})
  get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
  set(C_FILE ${CMAKE_CURRENT_LIST_DIR}/kernel/mrb/${RB_NAME}.c)
  add_custom_command(
    OUTPUT ${C_FILE}
    COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${RB_FILE}..."
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_LIST_DIR}/kernel/mrb
    COMMAND ${PICORBC} -B${RB_NAME}_irep -o${C_FILE} ${RB_FILE}
    DEPENDS ${RB_FILE}
    COMMENT "Compiling ${RB_FILE} to bytecode"
    VERBATIM
  )
endforeach()

# Generate C files from default_app Ruby files
foreach(RB_FILE ${DEFAULT_APP_RUBY_FILES})
  get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
  set(C_FILE ${CMAKE_CURRENT_LIST_DIR}/app/default_app/mrb/${RB_NAME}.c)
  add_custom_command(
    OUTPUT ${C_FILE}
    COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${RB_FILE}..."
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_LIST_DIR}/app/default_app/mrb
    COMMAND ${PICORBC} -B${RB_NAME}_irep -o${C_FILE} ${RB_FILE}
    DEPENDS ${RB_FILE}
    COMMENT "Compiling ${RB_FILE} to bytecode"
    VERBATIM
  )
endforeach()