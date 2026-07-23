# Compile PreBuild Ruby to C via the Spinel AOT compiler (library mode).
#
# Unlike the picorbc bytecode path (compile_ruby_to_bytecode.cmake), the Spinel
# compiler binary lives in the fork checkout (family-mruby/tmp/spinel), which is
# NOT mounted into the ESP-IDF docker build. So the .c is generated on the HOST
# by `rake spinel:gen` before the build, and CMake only adds the pre-generated
# file to the sources here. If SPINEL_BIN is set and exists (e.g. a host build
# outside docker), an add_custom_command regenerates it on .rb change.
#
# The generated program is compiled with `spinel --no-main --entry <name>_entry`
# so it exposes `int <name>_entry(void)` for a C task wrapper to call
# (fmrb_kernel.c). It links against components/fmrb_spinel_rt, which propagates
# SP_GC_STACK_MAX and the spinel_rt include dir via INTERFACE.

# Add the pre-generated Spinel C to COMPONENT_SRCS. Call BEFORE
# idf_component_register.
function(prepare_ruby_spinel_source RB_NAME GEN_DIR COMPONENT_SRCS_VAR)
  set(C_FILE ${GEN_DIR}/${RB_NAME}.c)
  list(APPEND ${COMPONENT_SRCS_VAR} ${C_FILE})
  set(${COMPONENT_SRCS_VAR} ${${COMPONENT_SRCS_VAR}} PARENT_SCOPE)
endfunction()

# Set up generation of the Spinel C. Call AFTER idf_component_register.
#   RB_FILE  - absolute path to the .rb entry program
#   ENTRY    - entry function name (C sees `int <ENTRY>(void)`)
#   GEN_DIR  - output directory for <name>.c
# If SPINEL_BIN is defined and exists, add a custom command to (re)generate on
# change; otherwise require the file to already exist (host pre-generated).
function(generate_ruby_spinel_command RB_FILE ENTRY GEN_DIR)
  get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
  get_filename_component(RB_DIR ${RB_FILE} DIRECTORY)
  set(C_FILE ${GEN_DIR}/${RB_NAME}.c)

  if(DEFINED SPINEL_BIN AND EXISTS ${SPINEL_BIN})
    file(MAKE_DIRECTORY ${GEN_DIR})
    add_custom_command(
      OUTPUT ${C_FILE}
      COMMAND ${SPINEL_BIN} --no-main --entry ${ENTRY} -I ${RB_DIR} -c ${RB_FILE} -o ${C_FILE}
      DEPENDS ${RB_FILE}
      COMMENT "Spinel compiling ${RB_NAME}.rb -> ${RB_NAME}.c (entry ${ENTRY})"
      VERBATIM
    )
  elseif(NOT EXISTS ${C_FILE})
    message(FATAL_ERROR
      "Spinel-generated ${C_FILE} not found and SPINEL_BIN is unset. "
      "Run `rake spinel:gen` on the host first (it uses tmp/spinel/bin/spinel, "
      "which is not available inside the docker build).")
  endif()
endfunction()
