set(COMPONENT_SRCS
    "main.c"
    "kernel/fmrb_kernel.c"
    "kernel/system/system_task.c"
)

# Ruby files to compile to bytecode
set(RUBY_FILES kernel)
set(PICORBC ${CMAKE_CURRENT_LIST_DIR}/../components/picoruby-esp32/picoruby/bin/picorbc)

# Generate C files from Ruby files
foreach(rb ${RUBY_FILES})
  set(C_FILE ${CMAKE_CURRENT_LIST_DIR}/kernel/mrb/${rb}.c)
  set(RB_FILE ${CMAKE_CURRENT_LIST_DIR}/kernel/mrblib/${rb}.rb)
  add_custom_command(
    OUTPUT ${C_FILE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_LIST_DIR}/kernel/mrb
    COMMAND ${PICORBC} -B${rb}_irep -o${C_FILE} ${RB_FILE}
    DEPENDS ${RB_FILE}
    COMMENT "Compiling ${RB_FILE} to bytecode"
  )
  list(APPEND COMPONENT_SRCS ${C_FILE})
endforeach()

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "." "kernel" "kernel/system"
    REQUIRES fmrb_hal fmrb_ipc fmrb_gfx fmrb_audio picoruby-esp32 log
)