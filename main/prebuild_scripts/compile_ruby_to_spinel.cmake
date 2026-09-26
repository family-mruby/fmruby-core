# Compile PreBuild Ruby to C via the Spinel AOT compiler (ext programs).
#
# Unlike the picorbc bytecode path (compile_ruby_to_bytecode.cmake), the Spinel
# compiler binary lives in the fork checkout (vendor/spinel or SPINEL_DIR),
# which is NOT mounted into the ESP-IDF docker build. So the .c is generated on
# the HOST by `rake spinel:gen` before the build, and CMake only adds the
# pre-generated file to the sources here. If SPINEL_BIN is set and exists (e.g.
# a host build outside docker), an add_custom_command regenerates it on .rb
# change.
#
# Every program is compiled as a Spinel ext program: `spinel --ext-init <init>`
# gives it `void <init>(void)`, which runs the program's top level, and a
# contract header (<name>.h) next to the .c. A VM (kernel, editor, desktop)
# has no entries: its task calls the init once and the Ruby main loop runs
# inside it. A gem (fft, raycast, spinel_hello) also has `--ext-entry` typed
# entries its native/ receiver calls through the header. The generated program
# links against components/fmrb_spinel_rt, which propagates SP_GC_STACK_MAX and
# the spinel_rt include dir via INTERFACE.

# Add the pre-generated Spinel C to COMPONENT_SRCS. Call BEFORE
# idf_component_register.
function(prepare_ruby_spinel_source RB_NAME GEN_DIR COMPONENT_SRCS_VAR)
  set(C_FILE ${GEN_DIR}/${RB_NAME}.c)
  list(APPEND ${COMPONENT_SRCS_VAR} ${C_FILE})
  set(${COMPONENT_SRCS_VAR} ${${COMPONENT_SRCS_VAR}} PARENT_SCOPE)
endfunction()

# Set up generation of the Spinel C. Call AFTER idf_component_register.
#   RB_FILE  - absolute path to the .rb program
#   INIT     - init function name (C sees `void <INIT>(void)`). For a gem, pass
#              GEM: the init and the entries are then read from the program's
#              `# spinel-ext-init:` / `# spinel-ext-entry:` comment lines, the
#              same lines rakelib/spinel.rake reads.
#   GEN_DIR  - output directory for <name>.c / <name>.h
# --no-inline-hot is passed to every program here, as rakelib/spinel.rake's
# gen_flags does (forced inlining overflowed the kernel task stack on the
# device). This path only runs when SPINEL_BIN is set (a host build outside
# docker), so a flag added to one and not the other produces a different
# program from the same source depending on how you built -- silently.
# If SPINEL_BIN is defined and exists, add a custom command to (re)generate on
# change; otherwise require the file to already exist (host pre-generated).
function(generate_ruby_spinel_command RB_FILE INIT GEN_DIR)
  get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
  get_filename_component(RB_DIR ${RB_FILE} DIRECTORY)
  set(C_FILE ${GEN_DIR}/${RB_NAME}.c)
  set(H_FILE ${GEN_DIR}/${RB_NAME}.h)
  set(EXT_FLAGS --ext-init ${INIT})
  if(INIT STREQUAL "GEM")
    set(EXT_FLAGS "")
    if(EXISTS ${RB_FILE})
      file(STRINGS ${RB_FILE} _init REGEX "^# spinel-ext-init: *[^ ]+")
      file(STRINGS ${RB_FILE} _entry REGEX "^# spinel-ext-entry: *[^ ]+")
      string(REGEX REPLACE "^# spinel-ext-init: *" "" _init "${_init}")
      string(REGEX REPLACE "^# spinel-ext-entry: *" "" _entry "${_entry}")
      set(EXT_FLAGS --ext-init ${_init} --ext-entry ${_entry})
    endif()
  endif()

  set(REGEN FALSE)
  if(DEFINED SPINEL_BIN AND EXT_FLAGS)
    if(EXISTS "${SPINEL_BIN}")
      set(REGEN TRUE)
    endif()
  endif()
  if(REGEN)
    file(MAKE_DIRECTORY ${GEN_DIR})
    add_custom_command(
      OUTPUT ${C_FILE} ${H_FILE}
      COMMAND ${SPINEL_BIN} --no-inline-hot -I ${RB_DIR} -c ${RB_FILE} ${EXT_FLAGS} -o ${C_FILE}
      DEPENDS ${RB_FILE}
      COMMENT "Spinel compiling ${RB_NAME}.rb -> ${RB_NAME}.c/.h"
      VERBATIM
    )
  elseif(NOT EXISTS ${C_FILE})
    message(FATAL_ERROR
      "Spinel-generated ${C_FILE} not found and SPINEL_BIN is unset. "
      "Run `rake spinel:gen` on the host first (it uses vendor/spinel/bin/spinel, "
      "which is not available inside the docker build).")
  endif()
endfunction()
