# Compile Ruby files to C bytecode files
# This script provides CMake functions to compile Ruby files to C bytecode

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
  set(PICORBC ${CMAKE_CURRENT_LIST_DIR}/../components/picoruby-esp32/picoruby/bin/picorbc)

  # Get the list of Ruby files from the variable
  set(RUBY_FILES ${${RUBY_FILES_VAR}})

  foreach(RB_FILE ${RUBY_FILES})
    get_filename_component(RB_NAME ${RB_FILE} NAME_WE)
    set(C_FILE ${OUTPUT_DIR}/${RB_NAME}.c)

    # Create custom command to compile Ruby to C
    add_custom_command(
      OUTPUT ${C_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${RB_FILE}..."
      COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
      COMMAND ${PICORBC} -B${RB_NAME}_irep -o${C_FILE} ${RB_FILE}
      DEPENDS ${RB_FILE}
      COMMENT "Compiling ${RB_FILE} to bytecode"
      VERBATIM
    )
  endforeach()
endfunction()
