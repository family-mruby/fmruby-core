# MicroPython guest VM.
#
# mp_embed/ is the output of "rake micropython:gen" (the MicroPython embed
# port), committed so this build compiles plain C. Its source list comes from
# mp_embed_srcs.cmake, which the same task rewrites -- do not glob and do not
# hand-edit either of them; change port/mpconfigport.h and regenerate.
include(${CMAKE_CURRENT_LIST_DIR}/mp_embed_srcs.cmake)

set(FMRB_MP_SRCS
    "fmrb_mp.c"
    "port/mpport.c"
)

idf_component_register(
    SRCS ${MP_EMBED_SRCS} ${FMRB_MP_SRCS}
    INCLUDE_DIRS "include" "mp_embed" "port"
    REQUIRES fmrb_mem fmrb_common fmrb_log log
)

# embed_util.c defines __assert_func when NDEBUG is absent, as an infinite
# loop. newlib declares the same symbol, so on ESP32 that definition wins for
# the whole firmware and every failed assert() anywhere would hang silently
# instead of aborting with a message. NDEBUG on this one translation unit drops
# the definition; the file has no assert() of its own to lose.
set_source_files_properties(mp_embed/port/embed_util.c
    PROPERTIES COMPILE_DEFINITIONS "NDEBUG")
