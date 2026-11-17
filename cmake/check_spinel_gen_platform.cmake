# Refuse to compile Spinel C that rake spinel:gen made for the other platform.
# Run as a build step (cmake -P) before the main component compiles, from a
# target in main/CMakeLists.txt, so it is checked on every build -- `idf.py
# flash` included, which builds again before it writes.
#
# Why this exists: the generated programs (main/ruby/spinel/gen) are made on
# the host, once per build, with the platform baked in (FmrbConst::PLATFORM,
# which gates ESP32-only code such as the RTC). The Linux sim (build-linux/)
# and the ESP32 firmware (build/) build side by side and share that one
# directory, so `rake build:linux` after `rake build:esp32` leaves Linux
# programs there; a later idf.py build or flash in build/ would compile them
# into the board's firmware and say nothing.
#
# Expected inputs (passed with -D):
#   GEN_DIR     main/ruby/spinel/gen
#   IDF_TARGET  linux / esp32s3 / esp32p4

set(CONST_RB "${GEN_DIR}/fmrb_const_generated.rb")
if(NOT EXISTS "${CONST_RB}")
    # No Spinel VM in this configuration: the gem programs carry no platform.
    return()
endif()
if(IDF_TARGET STREQUAL "linux")
    set(EXPECTED "linux")
    set(HINT "rake build:linux")
else()
    set(EXPECTED "esp32")
    set(HINT "rake build:esp32 (with the FMRB_HW_TARGET of this build)")
endif()
file(STRINGS "${CONST_RB}" LINE REGEX "^ *PLATFORM = \"[a-z0-9]+\"" LIMIT_COUNT 1)
string(REGEX REPLACE "^ *PLATFORM = \"([a-z0-9]+)\".*" "\\1" GOT "${LINE}")
if(NOT GOT STREQUAL EXPECTED)
    message(FATAL_ERROR
        "Spinel programs in ${GEN_DIR} were generated for '${GOT}', but this is "
        "a ${IDF_TARGET} build (the sim and the ESP32 build share that directory).\n"
        "Run ${HINT}: it generates them again for this platform before building.")
endif()
