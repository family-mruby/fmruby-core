# Refuse to build a storage image whose settings are missing or belong to
# another board. Run as a build step (cmake -P) from the storage_staging
# target in the top level CMakeLists.txt, so it is checked on every build and
# not only when cmake reconfigures.
#
# Why this exists: flash/etc/system_conf.toml and its factory copy are not in
# git. rake build:esp32 writes them from config/system_conf_<chip>.toml before
# it calls idf.py, so the rake path is always right -- but a plain "idf.py
# build" produced an image with no settings at all on a fresh checkout, or
# with the previous target's settings after switching boards, and said nothing
# about it either time.
#
# The factory copy is the one that matters most. The kernel boots from
# /etc/system_conf.toml, falls back to /etc/system_conf.factory.toml when that
# file is unreadable (the desktop rewrites it, and power can go mid-save), and
# gives up if neither loads: no desktop, no radio, nothing but USB. An image
# without the factory copy is one power cut away from a board that has to be
# reflashed.
#
# Expected inputs (passed with -D):
#   SRC_DIR         repository root
#   IDF_TARGET      esp32s3 / esp32p4
#   FMRB_HW_TARGET  NARYAv3 / TAB5 / NARYAv4 / ATOM_DISPLAY

set(ETC_DIR "${SRC_DIR}/flash/etc")
set(LIVE "${ETC_DIR}/system_conf.toml")
set(FACTORY "${ETC_DIR}/system_conf.factory.toml")

# Mirrors the hw_config table in rakelib/build.rake. Kept in step by this
# check itself: if the two disagree, the comparison below fails the build
# rather than shipping the wrong settings.
if(IDF_TARGET STREQUAL "esp32p4")
    if(FMRB_HW_TARGET STREQUAL "NARYAv4")
        set(EXPECTED "${SRC_DIR}/config/system_conf_naryav4.toml")
    else()
        set(EXPECTED "${SRC_DIR}/config/system_conf_p4.toml")
    endif()
elseif(FMRB_HW_TARGET STREQUAL "ATOM_DISPLAY")
    set(EXPECTED "${SRC_DIR}/config/system_conf_n8r8.toml")
else()
    set(EXPECTED "${SRC_DIR}/config/system_conf_n16r8.toml")
endif()

set(HINT "Run 'rake build:esp32' (it stages these before calling idf.py) rather than idf.py directly.")

if(NOT EXISTS "${LIVE}")
    message(FATAL_ERROR
        "storage image: ${LIVE} is missing.\n"
        "The board would boot with no settings.\n${HINT}")
endif()
if(NOT EXISTS "${FACTORY}")
    message(FATAL_ERROR
        "storage image: ${FACTORY} is missing.\n"
        "That is the copy the kernel falls back to when the live settings are "
        "damaged; without it a bad save leaves the board unbootable.\n${HINT}")
endif()

if(NOT EXISTS "${EXPECTED}")
    message(FATAL_ERROR
        "storage image: expected settings source ${EXPECTED} does not exist. "
        "The board table in this check and in rakelib/build.rake disagree.")
endif()

# The live file is compared too, not just the factory one: the Linux
# simulation runs out of this same flash/ tree and rewrites it whenever
# settings are changed there, so a build that skipped rake can otherwise bake
# a simulator's settings into a board image.
file(SHA256 "${EXPECTED}" EXPECTED_HASH)
file(SHA256 "${LIVE}" LIVE_HASH)
file(SHA256 "${FACTORY}" FACTORY_HASH)

if(NOT LIVE_HASH STREQUAL EXPECTED_HASH)
    message(FATAL_ERROR
        "storage image: ${LIVE} does not match ${EXPECTED}, which is the "
        "settings file for ${FMRB_HW_TARGET} (${IDF_TARGET}).\n"
        "This is what shipping another board's settings looks like.\n${HINT}")
endif()
if(NOT FACTORY_HASH STREQUAL EXPECTED_HASH)
    message(FATAL_ERROR
        "storage image: ${FACTORY} does not match ${EXPECTED}. The factory "
        "copy is written once at build time and never at runtime, so it "
        "should be the same file the image ships as the live settings.\n${HINT}")
endif()

# Not fatal: a development board needs these credentials to join a network,
# and they are only ever staged when the developer has put config/wifi.toml
# in place (it is not in git, so CI never has one). It is worth saying out
# loud, because an image built this way carries them.
if(EXISTS "${ETC_DIR}/wifi.toml")
    message(STATUS
        "storage image: flash/etc/wifi.toml is included -- this image carries "
        "WiFi credentials and should not be distributed")
endif()

message(STATUS "storage image: settings verified against ${EXPECTED}")
