# FMRB_HW_* compile macros shared by every component that includes
# fmrb_pin_assign.h (main, fmrb_hal, picoruby-esp32 gem ports).
#
# Two independent axes:
#   Chip generation (from IDF_TARGET):
#     FMRB_HW_MODERN       ESP32-P4 (board-independent Modern code)
#   Board (from FMRB_HW_TARGET):
#     FMRB_HW_TAB5         M5Stack Tab5 (current Modern dev board)
#     FMRB_HW_NARYAV4      Future dedicated Narya v4 P4 board (not designed
#                          yet; builds with the Tab5 pin assignment as a
#                          placeholder)
#     FMRB_HW_ATOM_DISPLAY AtomS3 + Atom Display (Retro)
#     (no macro)           Narya v3 (Retro default)
#
# Call fmrb_add_hw_defines() AFTER idf_component_register: IDF's early
# requirement scan runs component CMakeLists in script mode (stopping at the
# register call), where target_compile_definitions does not exist.
function(fmrb_add_hw_defines)
    if(IDF_TARGET STREQUAL "linux")
        return()
    endif()
    if(IDF_TARGET STREQUAL "esp32p4")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_MODERN)
        if(FMRB_HW_TARGET STREQUAL "NARYAv4")
            target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_NARYAV4)
        else()
            target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_TAB5)
        endif()
    elseif(FMRB_HW_TARGET STREQUAL "ATOM_DISPLAY")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_ATOM_DISPLAY)
    endif()
endfunction()
