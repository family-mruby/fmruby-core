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
#     FMRB_HW_NARYAV4      Narya v4, the dedicated Modern board being
#                          designed. Its pins are those of the bring-up
#                          hardware it is modelled on, an ESP32-P4-Nano
#                          (doc/naryav4/).
#     FMRB_HW_ATOM_DISPLAY AtomS3 + Atom Display (Retro)
#     FMRB_HW_ATOMS3R      M5Stack AtomS3R, an ESP32-S3 with a 128x128 LCD of
#                          its own and no child chip (doc/headless_s3/)
#     (no macro)           Narya v3 (Retro default)
#   Machine family, besides FMRB_HW_FAMILY_MODERN above:
#     FMRB_HW_FAMILY_MINI  a small single-app machine (AtomS3R). Neither
#                          Retro (no WROVER) nor Modern (no P4).
#   What the board has, for code that asks about a capability rather than a
#   chip or a board:
#     FMRB_HAS_LOCAL_DISPLAY  this chip composites and drives the screen
#                          itself (display_p4_task + the local link), instead
#                          of shipping draw commands to a child chip. Both P4
#                          boards and the AtomS3R. (The wasm build is the same
#                          shape and says so with FMRB_PLATFORM_WASM.)
#     FMRB_HAS_PPA         the P4's 2D engine (PPA) and JPEG codec exist.
#     FMRB_HAS_USB_HOST    a USB host port for keyboards and mice.
#     FMRB_HAS_WIFI        a radio for WiFi STA.
#   Build options, for every board (rake passes them in the environment):
#     FMRB_NO_FONT_JA      built with FMRB_FONT_JA=0: the efontJA fonts
#                          (16 / 12 / 12 bold) are not in the firmware and
#                          set_font(:ja, ...) selects misaki 8 instead.
#                          Where the fonts live in another chip's firmware
#                          (Retro's WROVER) only the selection changes.
#
# fmrb_hw_family_is_modern(<out_var>) answers the same question at configure
# time, for build lists rather than compile macros: whether this build is a
# Modern machine or a simulator standing in for one. Callable before
# idf_component_register (it only reads variables), so a CMakeLists can drop
# Modern-only sources from a glob.
function(fmrb_hw_family_is_modern out_var)
    if(IDF_TARGET STREQUAL "esp32p4")
        set(${out_var} TRUE PARENT_SCOPE)
    elseif(IDF_TARGET STREQUAL "linux" AND FMRB_HW_FAMILY STREQUAL "modern")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

# Call fmrb_add_hw_defines() AFTER idf_component_register: IDF's early
# requirement scan runs component CMakeLists in script mode (stopping at the
# register call), where target_compile_definitions does not exist.
function(fmrb_add_hw_defines)
    if("$ENV{FMRB_FONT_JA}" STREQUAL "0")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_NO_FONT_JA)
    endif()
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
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            FMRB_HAS_LOCAL_DISPLAY FMRB_HAS_PPA FMRB_HAS_USB_HOST)
        # WiFi STA: radio on the C6 coprocessor via esp_wifi_remote
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HAS_WIFI)
    elseif(FMRB_HW_TARGET STREQUAL "ATOM_DISPLAY")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_ATOM_DISPLAY)
    elseif(FMRB_HW_TARGET STREQUAL "ATOMS3R")
        # AtomS3R (ESP32-S3): draws on its own panel, native WiFi, no USB
        # host (the only USB port is the console).
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            FMRB_HW_ATOMS3R FMRB_HW_FAMILY_MINI FMRB_HAS_LOCAL_DISPLAY
            FMRB_HAS_WIFI)
    else()
        # Narya v3 (Retro, ESP32-S3): native WiFi. Runtime policy keeps it
        # mutually exclusive with BLE (no coexistence, see wifi_task.c).
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            FMRB_HAS_WIFI FMRB_HAS_USB_HOST)
    endif()
endfunction()

# Configure-time twin of FMRB_HAS_LOCAL_DISPLAY, for source lists: whether this
# firmware composites and drives its own screen (the display_p4 sources and
# the local link) rather than talking to a child chip.
function(fmrb_hw_has_local_display out_var)
    if(IDF_TARGET STREQUAL "esp32p4" OR FMRB_HW_TARGET STREQUAL "ATOMS3R")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()
