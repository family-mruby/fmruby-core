# Kanji dictionary lookup (doc/kanji_input/). Pure C over the file HAL and the
# SYSTEM pool: the same source runs on the ESP32 boards, the simulation and
# the native program (hosted/hosted.cmake lists it there). The browser build
# leaves it out -- it converts with the browser's own IME (K4).
idf_component_register(
    SRCS "fmrb_kanji.c" "fmrb_kanji_cands.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_common fmrb_hal fmrb_mem
)

include(${CMAKE_CURRENT_LIST_DIR}/../fmrb_common/fmrb_hw_defines.cmake)
fmrb_add_hw_defines()
