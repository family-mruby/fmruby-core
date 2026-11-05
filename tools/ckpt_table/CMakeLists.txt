# tools/ckpt_table/CMakeLists.txt - the checkpoint table of a device
# configuration (P4, S3), made on the host (doc/checkpoint/flash_table.md, Ff;
# `rake checkpoint:table` configures and builds this).
#
#   -DFMRB_CKPT_OF=esp32 | esp32p4   the libmruby: picoruby build ckpt-table-<of>
#                                    (lib/add/family_mruby_ckpt_table.rb)
#   -DIDF_TARGET=esp32s3 | esp32p4   the chip and
#   -DFMRB_HW_TARGET=TAB5 | ...      the board: their definitions are fmrb_hw_defines.cmake's
#   -DFMRB_CKPT_OUT=file.c           the table
#
# The gem ports are the hosted builds' (their ports/esp32 sources), compiled
# as the Linux targets compile them -- the device's ESP-IDF branches cannot
# be compiled here -- but with the device's definitions otherwise: its board,
# its app ceiling, without the hosted program's additions. What differs
# between the two branches is a constant's value (kept by each VM, not by the
# table) or, rarely, a name; a VM whose state differs from the table keeps
# the difference itself and says so (mrb_checkpoint_table_status). The
# table is compiled once more as a 32-bit object, the device's pointer width.

cmake_minimum_required(VERSION 3.16)
project(fmrb_ckpt_table C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(FMRB_HOSTED_PLATFORM native)
set(FMRB_HOSTED_MRUBY_BUILD ckpt-table-${FMRB_CKPT_OF})
set(FMRB_HOSTED_NATIVE_ISA ON)
include(${CMAKE_CURRENT_LIST_DIR}/../../hosted/hosted.cmake)

# The hosted program's definitions, less the sizes and the profile only the
# one-process program has, plus the device's. FMRB_PLATFORM_HOSTED stays: it
# is what lets the shared headers compile without ESP-IDF (the ports define
# the same classes and methods with it or without it).
set(defines ${FMRB_HOSTED_CORE_DEFINES})
list(REMOVE_ITEM defines FMRB_HW_FAMILY_MODERN
     FMRB_MAX_APPS=32 DISPLAY_LOCAL_MAX_CANVAS=34 DISPLAY_LOCAL_VM_MAX_PROGS=64
     MRB_BASELINE_PROFILE=1 MRB_TASK_TICK_SELF_SUPPLY)
list(APPEND defines MRB_CONSTRAINED_BASELINE_PROFILE=1 MRB_VM_SAFEPOINT_HOOK FMRB_HAVE_CHECKPOINT)

add_library(ckpt_table_objs OBJECT
  ${FMRB_HOSTED_GEM_PORT_SRCS}
  ${FMRB_ROOT}/main/app/fmrb_app_checkpoint.c
  ${CMAKE_CURRENT_LIST_DIR}/gen_main.c)
target_include_directories(ckpt_table_objs PRIVATE ${FMRB_HOSTED_CORE_INCLUDES}
  ${MRBGEMS}/mruby-checkpoint/include)
target_compile_definitions(ckpt_table_objs PRIVATE ${defines})
target_compile_options(ckpt_table_objs PRIVATE -pthread -Wno-format)
target_link_libraries(ckpt_table_objs PUBLIC freertos_hosted)
# the board's definitions, as the IDF components get them
include(${FMRB_ROOT}/components/fmrb_common/fmrb_hw_defines.cmake)
set(COMPONENT_LIB ckpt_table_objs)
fmrb_add_hw_defines()

find_program(FMRB_RUBY ruby REQUIRED)
add_custom_command(OUTPUT ${FMRB_CKPT_OUT}
  COMMAND ${FMRB_RUBY} ${CMAKE_CURRENT_LIST_DIR}/ckpt_table.rb
          --cc ${CMAKE_C_COMPILER} --work ${CMAKE_BINARY_DIR}/work --out ${FMRB_CKPT_OUT}
          -- $<TARGET_OBJECTS:ckpt_table_objs> ${FMRB_HOSTED_LIBMRUBY} -lm -pthread
  DEPENDS ckpt_table_objs $<TARGET_OBJECTS:ckpt_table_objs> ${FMRB_HOSTED_LIBMRUBY}
          ${CMAKE_CURRENT_LIST_DIR}/ckpt_table.rb
  COMMAND_EXPAND_LISTS
  COMMENT "Making the checkpoint table of ${FMRB_CKPT_OF}")

# the table as the device compiles it: 32-bit, the device's mruby definitions
set(obj ${CMAKE_BINARY_DIR}/fmrb_checkpoint_table.32.o)
add_custom_command(OUTPUT ${obj}
  COMMAND ${CMAKE_C_COMPILER} -m32 -std=gnu11 -Wall -Werror -c ${FMRB_CKPT_OUT} -o ${obj}
          -DMRB_32BIT -DMRB_INT64 -DMRB_NO_BOXING -DMRB_UTF8_STRING
          -I${MRBGEMS}/picoruby-mruby/lib/mruby/include -I${FMRB_HOSTED_MRUBY_DIR}/include
          -I${MRBGEMS}/mruby-checkpoint/include
  DEPENDS ${FMRB_CKPT_OUT}
  COMMENT "Compiling the table as a 32-bit object")
add_custom_target(ckpt_table ALL DEPENDS ${FMRB_CKPT_OUT} ${obj})
