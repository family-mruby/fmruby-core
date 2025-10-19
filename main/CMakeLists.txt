set(COMPONENT_SRCS
    "main.c"
    "boot.c"
    "kernel/fmrb_kernel.c"
    "kernel/system/system_task.c"
)

# Collect all Ruby files to compile to bytecode
set(PICORBC ${CMAKE_CURRENT_LIST_DIR}/../components/picoruby-esp32/picoruby/bin/picorbc)
set(GENERATED_C_FILES "")

# Find all Ruby files in kernel/mrblib/
file(GLOB KERNEL_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/kernel/mrblib/*.rb)

# Find all Ruby files in app/system_app/mrblib/
file(GLOB SYSTEM_APP_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/app/system_app/mrblib/*.rb)

# Process kernel Ruby files
foreach(RB_FILE ${KERNEL_RUBY_FILES})
  get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
  set(C_FILE ${CMAKE_CURRENT_LIST_DIR}/kernel/mrb/${RB_NAME}.c)
  list(APPEND GENERATED_C_FILES ${C_FILE})
  list(APPEND COMPONENT_SRCS ${C_FILE})
endforeach()

# Process system_app Ruby files
foreach(RB_FILE ${SYSTEM_APP_RUBY_FILES})
  get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
  set(C_FILE ${CMAKE_CURRENT_LIST_DIR}/app/system_app/mrb/${RB_NAME}.c)
  list(APPEND GENERATED_C_FILES ${C_FILE})
  list(APPEND COMPONENT_SRCS ${C_FILE})
endforeach()

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "." "kernel" "kernel/system"
    REQUIRES fmrb_hal fmrb_ipc fmrb_gfx fmrb_audio picoruby-esp32 log
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

# Generate C files from system_app Ruby files
foreach(RB_FILE ${SYSTEM_APP_RUBY_FILES})
  get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
  set(C_FILE ${CMAKE_CURRENT_LIST_DIR}/app/system_app/mrb/${RB_NAME}.c)
  add_custom_command(
    OUTPUT ${C_FILE}
    COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${RB_FILE}..."
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_LIST_DIR}/app/system_app/mrb
    COMMAND ${PICORBC} -B${RB_NAME}_irep -o${C_FILE} ${RB_FILE}
    DEPENDS ${RB_FILE}
    COMMENT "Compiling ${RB_FILE} to bytecode"
    VERBATIM
  )
endforeach()