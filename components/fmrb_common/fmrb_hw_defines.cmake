# FMRB_HW_* compile macros shared by every component that includes
# fmrb_pin_assign.h (main, fmrb_hal, picoruby-esp32 gem ports).
#
# Two independent axes:
#   Chip generation (from IDF_TARGET):
#     FMRB_HW_MODERN       ESP32-P4 (board-independent Modern code)
#     FMRB_HW_FAMILY_MODERN  This build is a Modern machine, or a simulator
#                          standing in for one. Unlike FMRB_HW_MODERN it is
#                          also set for the Linux build (from FMRB_HW_FAMILY,
#                          which rake passes from FMRB_HW_TARGET), so code
#                          that only asks "which machine's habits?" -- what
#                          fonts exist, say -- gets the same answer in the
#                          simulator as on the device. Code that touches P4
#                          hardware must keep using FMRB_HW_MODERN.
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
        # The simulator has no hardware to speak of, but it does stand in for
        # one machine or the other, and rake says which.
        if(FMRB_HW_FAMILY STREQUAL "modern")
            target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_FAMILY_MODERN)
        endif()
        return()
    endif()
    if(IDF_TARGET STREQUAL "esp32p4")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_MODERN)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_FAMILY_MODERN)
        if(FMRB_HW_TARGET STREQUAL "NARYAv4")
            target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_NARYAV4)
        else()
            target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_TAB5)
        endif()
        # WiFi STA: radio on the C6 coprocessor via esp_wifi_remote
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HAS_WIFI)
    elseif(FMRB_HW_TARGET STREQUAL "ATOM_DISPLAY")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_ATOM_DISPLAY)
    else()
        # Narya v3 (Retro, ESP32-S3): native WiFi. Runtime policy keeps it
        # mutually exclusive with BLE (no coexistence, see wifi_task.c).
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HAS_WIFI)
    endif()
endfunction()
