# Compile Ruby files to C bytecode files
# This script provides CMake functions to compile Ruby files to C bytecode
#
# Supports subdirectory concatenation:
#   If a .app.rb file has a matching subdirectory (e.g., system_desktop/ for
#   system_desktop.app.rb), all .rb files in that subdirectory are prepended
#   to the main file before compilation. This allows splitting large scripts
#   into modules while keeping single-file picorbc compilation.

# Step 1: Prepare Ruby bytecode source files (call before idf_component_register)
# Arguments:
#   RUBY_FILES_VAR - Variable name containing list of Ruby files
#   OUTPUT_DIR - Directory where C files will be generated
#   COMPONENT_SRCS_VAR - Variable name to append generated C files to
function(prepare_ruby_bytecode_sources RUBY_FILES_VAR OUTPUT_DIR COMPONENT_SRCS_VAR)
  # Get the list of Ruby files from the variable
  set(RUBY_FILES ${${RUBY_FILES_VAR}})

  foreach(RB_FILE ${RUBY_FILES})
    get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
    set(C_FILE ${OUTPUT_DIR}/${RB_NAME}.c)

    # Add to component sources
    list(APPEND ${COMPONENT_SRCS_VAR} ${C_FILE})
  endforeach()

  # Update the parent scope variable
  set(${COMPONENT_SRCS_VAR} ${${COMPONENT_SRCS_VAR}} PARENT_SCOPE)
endfunction()

# Step 2: Generate Ruby bytecode compilation commands (call after idf_component_register)
# Arguments:
#   RUBY_FILES_VAR - Variable name containing list of Ruby files
#   OUTPUT_DIR - Directory where C files will be generated
function(generate_ruby_bytecode_commands RUBY_FILES_VAR OUTPUT_DIR)
  # Upstream renamed the prism-based compiler binary picorbc -> mrbc
  # (mruby-bin-mrbc). Same -B<sym> -o<out> <in> CLI.
  set(PICORBC ${CMAKE_CURRENT_LIST_DIR}/../components/picoruby-esp32/picoruby/bin/mrbc)

  # Get the list of Ruby files from the variable
  set(RUBY_FILES ${${RUBY_FILES_VAR}})

  foreach(RB_FILE ${RUBY_FILES})
    get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
    set(C_FILE ${OUTPUT_DIR}/${RB_NAME}.c)

    # Check for matching subdirectory (e.g., system_desktop/ for system_desktop.app.rb)
    # Strip .app suffix to get base name: "system_desktop.app" -> "system_desktop"
    string(REPLACE ".app" "" RB_BASE ${RB_NAME})
    get_filename_component(RB_DIR ${RB_FILE} DIRECTORY)
    set(SUBDIR ${RB_DIR}/${RB_BASE})

    if(IS_DIRECTORY ${SUBDIR})
      # Subdirectory exists - concatenate sub/*.rb + main.rb
      file(GLOB SUB_RB_FILES ${SUBDIR}/*.rb)
      list(SORT SUB_RB_FILES)  # Deterministic order

      set(COMBINED_FILE ${OUTPUT_DIR}/${RB_NAME}_combined.rb)
      set(COMBINED_MAP ${OUTPUT_DIR}/${RB_NAME}_combined.map.json)

      # Concatenate sub/*.rb + main.rb AND emit a line map (combined line ->
      # original file:line) for the remote debugger. Byte-identical to the old
      # `cat`, plus the .map.json side output (see tools/debug/gen_combined_rb.py).
      add_custom_command(
        OUTPUT ${COMBINED_FILE} ${COMBINED_MAP}
        COMMAND ${CMAKE_COMMAND} -E echo "Concatenating ${SUBDIR}/*.rb + ${RB_FILE} (+ line map)..."
        COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/../tools/debug/gen_combined_rb.py
                ${COMBINED_FILE} ${COMBINED_MAP} ${SUB_RB_FILES} ${RB_FILE}
        DEPENDS ${SUB_RB_FILES} ${RB_FILE}
                ${CMAKE_CURRENT_LIST_DIR}/../tools/debug/gen_combined_rb.py
        COMMENT "Concatenating subdirectory files for ${RB_NAME}"
        VERBATIM
      )

      # Compile concatenated file
      add_custom_command(
        OUTPUT ${C_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${COMBINED_FILE}..."
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
        COMMAND ${PICORBC} -g -B${RB_NAME}_irep -o${C_FILE} ${COMBINED_FILE}
        DEPENDS ${COMBINED_FILE}
        COMMENT "Compiling ${RB_NAME} (with subdirectory modules) to bytecode"
        VERBATIM
      )
    else()
      # No subdirectory - compile directly
      add_custom_command(
        OUTPUT ${C_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${RB_FILE}..."
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
        COMMAND ${PICORBC} -g -B${RB_NAME}_irep -o${C_FILE} ${RB_FILE}
        DEPENDS ${RB_FILE}
        COMMENT "Compiling ${RB_FILE} to bytecode"
        VERBATIM
      )
    endif()
  endforeach()
endfunction()
