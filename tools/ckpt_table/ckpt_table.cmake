# tools/ckpt_table/ckpt_table.cmake - make the checkpoint table of a hosted
# build as part of it (doc/checkpoint/flash_table.md, Ff).
#
#   fmrb_ckpt_table(<firmware target> <core objects target>
#                   [RUN <runner>] [LDFLAGS <flag>...])
#
# after include(hosted.cmake) and fmrb_hosted_core_objs(<core objects target>).
# The maker (gen_main.c) is compiled from the gem ports and the checkpoint
# glue with the core's own include paths and definitions (copied from the
# core objects target, so the platform's additions such as
# FMRB_PLATFORM_NATIVE are there too), linked against the same libmruby by
# ckpt_table.rb and run; the table it writes is a source of the firmware. A
# change to libmruby (its gems, their mrblib) or to the ports makes it again.

set(FMRB_CKPT_TABLE_DIR ${CMAKE_CURRENT_LIST_DIR})

function(fmrb_ckpt_table firmware core)
  cmake_parse_arguments(CT "" "RUN" "LDFLAGS" ${ARGN})
  set(objs ${firmware}_ckpt_table_objs)
  add_library(${objs} OBJECT
    ${FMRB_HOSTED_GEM_PORT_SRCS}
    ${FMRB_ROOT}/main/app/fmrb_app_checkpoint.c
    ${FMRB_CKPT_TABLE_DIR}/gen_main.c)
  target_include_directories(${objs} PRIVATE $<TARGET_PROPERTY:${core},INCLUDE_DIRECTORIES>)
  target_compile_definitions(${objs} PRIVATE $<TARGET_PROPERTY:${core},COMPILE_DEFINITIONS>)
  target_compile_options(${objs} PRIVATE $<TARGET_PROPERTY:${core},COMPILE_OPTIONS>)
  target_link_libraries(${objs} PUBLIC freertos_hosted)

  set(out ${CMAKE_BINARY_DIR}/ckpt_table/fmrb_checkpoint_table.c)
  set(args)
  if(CT_RUN)
    list(APPEND args --run "${CT_RUN}")
  endif()
  foreach(f ${CT_LDFLAGS})
    list(APPEND args "--ldflag=${f}")
  endforeach()
  find_program(FMRB_RUBY ruby REQUIRED)
  add_custom_command(OUTPUT ${out}
    COMMAND ${FMRB_RUBY} ${FMRB_CKPT_TABLE_DIR}/ckpt_table.rb
            --cc ${CMAKE_C_COMPILER} --work ${CMAKE_BINARY_DIR}/ckpt_table --out ${out} ${args}
            -- $<TARGET_OBJECTS:${objs}> ${FMRB_HOSTED_LIBMRUBY} -lm -pthread
    DEPENDS ${objs} $<TARGET_OBJECTS:${objs}> ${FMRB_HOSTED_LIBMRUBY} ${FMRB_CKPT_TABLE_DIR}/ckpt_table.rb
    COMMAND_EXPAND_LISTS
    COMMENT "Making the checkpoint table (mruby-checkpoint, flash_table.md)")
  target_sources(${firmware} PRIVATE ${out})
  target_include_directories(${firmware} PRIVATE
    ${MRBGEMS}/picoruby-mruby/lib/mruby/include ${FMRB_HOSTED_MRUBY_DIR}/include
    ${MRBGEMS}/mruby-checkpoint/include)
  target_compile_definitions(${firmware} PRIVATE $<TARGET_PROPERTY:${core},COMPILE_DEFINITIONS>)
endfunction()

# The same for the Linux simulation (ESP-IDF's linux target), called from
# main/CMakeLists.txt after idf_component_register:
#
#   fmrb_ckpt_table_idf(<main component library>)
#
# The maker is compiled from the gem ports the picoruby-esp32 component
# compiles for linux and the checkpoint glue of main, with the include paths,
# definitions and options of both components (their own and what they get
# from the components they require), linked by ckpt_table.rb against the
# rake-built libmruby (family-mruby-linux) and run in the build container
# (which has ruby: it builds libmruby with rake). The table is a source of
# main.
function(fmrb_ckpt_table_idf main_lib)
  set(root ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../..)
  set(mrbgems ${root}/components/picoruby-esp32/picoruby/mrbgems)
  set(mruby_build ${root}/components/picoruby-esp32/picoruby/build/family-mruby-linux)
  idf_component_get_property(pico_lib picoruby-esp32 COMPONENT_LIB)
  set(srcs)
  foreach(gem fmrb-app fmrb-kernel fmrb-const fmrb-log fmrb-io fmrb-filesystem
          fmrb-msgpack fmrb-bmp332 fmrb-fft fmrb-spinel-hello fmrb-raycast
          fmrb-debug fmrb-editor-core fmrb-midi)
    file(GLOB gem_srcs ${mrbgems}/picoruby-${gem}/ports/esp32/*.c)
    list(APPEND srcs ${gem_srcs})
  endforeach()
  # Two object libraries, each compiled as its component compiles it (the two
  # see different headers by the same name first). Their properties are read
  # through generator expressions, which take in what the components require;
  # they are not linked to them (main's own sources include the table made
  # here).
  add_library(ckpt_table_ports OBJECT ${srcs})
  add_library(ckpt_table_glue OBJECT ${root}/main/app/fmrb_app_checkpoint.c ${FMRB_CKPT_TABLE_DIR}/gen_main.c)
  foreach(pair "ckpt_table_ports:${pico_lib}" "ckpt_table_glue:${main_lib}")
    string(REPLACE ":" ";" pair ${pair})
    list(GET pair 0 objs)
    list(GET pair 1 lib)
    target_include_directories(${objs} PRIVATE $<TARGET_PROPERTY:${lib},INCLUDE_DIRECTORIES>
      ${mrbgems}/mruby-checkpoint/include)
    target_compile_definitions(${objs} PRIVATE $<TARGET_PROPERTY:${lib},COMPILE_DEFINITIONS>)
    target_compile_options(${objs} PRIVATE $<TARGET_PROPERTY:${lib},COMPILE_OPTIONS>)
    # the ports are compiled after libmruby (its generated headers)
    add_dependencies(${objs} picoruby)
  endforeach()

  set(out ${CMAKE_BINARY_DIR}/ckpt_table/fmrb_checkpoint_table.c)
  find_program(FMRB_RUBY ruby REQUIRED)
  add_custom_command(OUTPUT ${out}
    COMMAND ${FMRB_RUBY} ${FMRB_CKPT_TABLE_DIR}/ckpt_table.rb
            --cc ${CMAKE_C_COMPILER} --work ${CMAKE_BINARY_DIR}/ckpt_table --out ${out}
            -- $<TARGET_OBJECTS:ckpt_table_ports> $<TARGET_OBJECTS:ckpt_table_glue>
               ${mruby_build}/lib/libmruby.a -lm -pthread -lssl -lcrypto
    DEPENDS ckpt_table_ports ckpt_table_glue $<TARGET_OBJECTS:ckpt_table_ports>
            $<TARGET_OBJECTS:ckpt_table_glue> ${mruby_build}/lib/libmruby.a ${FMRB_CKPT_TABLE_DIR}/ckpt_table.rb
    COMMAND_EXPAND_LISTS
    COMMENT "Making the checkpoint table (mruby-checkpoint, flash_table.md)")
  target_sources(${main_lib} PRIVATE ${out})
  # main is an archive and fmrb_app_checkpoint.c refers to the table weakly:
  # without this the linker never takes the table's member out of it
  target_link_options(${main_lib} INTERFACE "-Wl,-u,fmrb_checkpoint_table")
endfunction()
