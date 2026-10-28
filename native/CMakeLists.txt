# Standalone build of the native Linux program (doc/linux_native/, L1-L2).
#
# The browser build's sibling: the same firmware set (hosted/hosted.cmake),
# the same cooperative FreeRTOS port, the Modern display task in the same
# process -- built with the host's gcc into one x86_64 executable, presenting
# through SDL2 instead of a page. Independent of idf.py, like wasm/; configure
# it through rake native:core (rakelib/native.rake).
#
#   fmruby-native   the program. Run it with rake native:run, or headless and
#                   driven with tools/fmrb_native.rb in the parent repository.
#
# libmruby comes from lib/add/family_mruby_native.rb (rake native:mruby --
# run it first); the Ruby bytecode and the Spinel programs from the existing
# generation steps (rake hosted:mrb via native:mrb, rake spinel:gen).

cmake_minimum_required(VERSION 3.16)
project(fmruby_native C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
# picoruby-socket's posix SSLSocket is OpenSSL, as on the Linux simulation.
pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)

# The device backends that need a library are built when it is there
# (native/backend/native_backends.c refuses a machine that names one this
# build does not have). -DNATIVE_WITH_DRM=OFF / -DNATIVE_WITH_ALSA=OFF leave
# them out on purpose. fbdev and evdev need only the kernel headers.
option(NATIVE_WITH_DRM "DRM/KMS output (libdrm)" ON)
option(NATIVE_WITH_ALSA "ALSA sound (libasound)" ON)
# The compositor (L8): wlroots 0.19 exactly (Ubuntu 26.04's; the API moves
# between releases). The developer's Ubuntu 22.04 has none, so the program
# is built without it there and with it in native/docker/Dockerfile.ubuntu
# (rake native:wl, native:deb ARCH=amd64).
option(NATIVE_WITH_WLROOTS "The wlroots compositor (wlroots-0.19)" ON)
set(NATIVE_HAVE_DRM 0)
set(NATIVE_HAVE_ALSA 0)
set(NATIVE_HAVE_WLROOTS 0)
if(NATIVE_WITH_DRM)
  pkg_check_modules(LIBDRM IMPORTED_TARGET libdrm)
  if(LIBDRM_FOUND)
    set(NATIVE_HAVE_DRM 1)
  endif()
endif()
if(NATIVE_WITH_ALSA)
  pkg_check_modules(ALSA IMPORTED_TARGET alsa)
  if(ALSA_FOUND)
    set(NATIVE_HAVE_ALSA 1)
  endif()
endif()
if(NATIVE_WITH_WLROOTS)
  pkg_check_modules(WLROOTS IMPORTED_TARGET wlroots-0.19 wayland-server xkbcommon)
  if(WLROOTS_FOUND)
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
    pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
    if(WAYLAND_PROTOCOLS_DIR AND WAYLAND_SCANNER)
      set(NATIVE_HAVE_WLROOTS 1)
    else()
      message(STATUS "wlroots-0.19 found, but not wayland-protocols / wayland-scanner: no compositor")
    endif()
  endif()
endif()
# The Linux applications' icons in the launcher (L9b, the compositor only):
# PNG through libpng when it is there; without it the entries have none.
set(NATIVE_HAVE_PNG 0)
if(NATIVE_HAVE_WLROOTS)
  pkg_check_modules(LIBPNG IMPORTED_TARGET libpng)
  if(LIBPNG_FOUND)
    set(NATIVE_HAVE_PNG 1)
  endif()
endif()
message(STATUS "native backends: sdl fbdev evdev; drm=${NATIVE_HAVE_DRM} alsa=${NATIVE_HAVE_ALSA} wlroots=${NATIVE_HAVE_WLROOTS} png=${NATIVE_HAVE_PNG}")

# ------------------------------------------- shared with the browser build

set(FMRB_HOSTED_PLATFORM native)
set(FMRB_HOSTED_MRUBY_BUILD family-mruby-native)
set(FMRB_HOSTED_NATIVE_ISA ON)
set(FMRB_HOSTED_PTR32 OFF)
include(${CMAKE_CURRENT_SOURCE_DIR}/../hosted/hosted.cmake)

# A package build (rake native:cross / native:deb, L3a) carries no path of
# this checkout: the program finds the distribution tree relative to its own
# installed executable (NATIVE_PACKAGE_DATA_REL; the .deb layout decides it
# per architecture) and keeps the user's files as family-mruby rather than
# family-mruby-dev. It also builds the small per-machine launchers
# (NATIVE_LAUNCHERS, "<suffix>=<machine>" each: family-mruby-<suffix> runs the
# program beside it with --machine <machine>).
option(NATIVE_PACKAGE "Build for an installed package (paths from the executable's place)" OFF)
set(NATIVE_PACKAGE_DATA_REL "../share/family-mruby/root" CACHE STRING
    "The distribution tree, relative to the installed executable's directory")
set(NATIVE_LAUNCHERS "" CACHE STRING "Launchers to build: suffix=machine;...")

# Where a development build looks by default: the repository's flash/ (the
# distribution tree, read-only) and the /etc that rake native:etc staged.
set(FMRB_NATIVE_SYSTEM_DIR ${FMRB_ROOT}/flash CACHE PATH "Default distribution tree")
set(FMRB_NATIVE_ETC_SEED ${CMAKE_CURRENT_SOURCE_DIR}/build/root/etc CACHE PATH "Default first-start /etc")
set(FMRB_NATIVE_MACHINES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/root/machines CACHE PATH "Default machine definitions")
get_filename_component(FMRB_NATIVE_SYSTEM_DIR_ABS ${FMRB_NATIVE_SYSTEM_DIR} ABSOLUTE)
get_filename_component(FMRB_NATIVE_ETC_SEED_ABS ${FMRB_NATIVE_ETC_SEED} ABSOLUTE)
get_filename_component(FMRB_NATIVE_MACHINES_DIR_ABS ${FMRB_NATIVE_MACHINES_DIR} ABSOLUTE)

# ------------------------------------------------------------------ core

fmrb_hosted_core_objs(core_objs)

set(N ${CMAKE_CURRENT_SOURCE_DIR})
set(NATIVE_BACKEND_SRCS
  ${N}/backend/main_native.c
  ${N}/backend/native_backends.c
  ${N}/backend/native_ctl.c
  # The machine's clipboard joined to Linux's (L12a)
  ${N}/backend/native_clip.c
  ${N}/backend/native_ime.c
  ${N}/backend/native_preedit.cpp
  ${N}/backend/native_frame.c
  ${N}/backend/png_write.c
  ${N}/backend/display_backend_native.cpp
  ${N}/backend/audio_backend_native.c
  ${N}/backend/input_native.c
  # The PC's window operations (L13): maximize, halves, back
  ${N}/backend/native_winops.c
  ${N}/backend/winops_geom.c
  ${N}/machine/native_machine.c
  ${N}/present/fmrb_present.c
  ${N}/output/output_sdl.c
  ${N}/output/output_fbdev.c
  ${N}/output/fbdev_info.c
  ${N}/audio/audio_sdl.c
  ${N}/input/linux_keys.c
  ${N}/input/keymap_cardputer_zero.c
  ${N}/input/evdev_decode.c
  ${N}/input/input_evdev.c
  # Networking (L2n): the blocking-call gate of the cooperative port, and the
  # wrappers that route the waiting network calls through it.
  ${FMRB_ROOT}/hosted/gate/fmrb_gate.c
  ${N}/net/net_wrap.c
  # The user's /etc brought up to the distribution's (L3a)
  ${N}/etc/etc_update.c
  # The OS services on Linux (L6): battery, network, mixer, power
  ${N}/sys/platform_sys_linux.c
  ${N}/sys/sys_linux_read.c
  ${N}/sys/sys_linux_mixer.c
  ${N}/sys/linux_pty.c
  # The compositor's hand-overs and Family mruby's side of its windows (L9a):
  # plain C, in every build (the stand-in's gem calls are always there; the
  # task only starts when the program is the compositor).
  ${N}/wlr/comp_route.c
  ${N}/wlr/comp_link.c
  ${N}/wlr/wl_bridge.c
  # mruby-dir's HAL over the file HAL's resolver (the ESP32 targets' file;
  # the name-only gem hal-dir-fmrb dropped the flat posix one from libmruby)
  ${FMRB_ROOT}/lib/add/hal-esp32-dir/dir_hal.c
)
if(NATIVE_HAVE_DRM)
  list(APPEND NATIVE_BACKEND_SRCS ${N}/output/output_drm.c)
endif()
if(NATIVE_HAVE_ALSA)
  list(APPEND NATIVE_BACKEND_SRCS ${N}/audio/audio_alsa.c)
endif()
if(NATIVE_HAVE_WLROOTS)
  # wlroots' xdg-shell header includes the protocol's server header, which
  # every compositor generates for itself.
  set(NATIVE_WLR_GEN ${CMAKE_CURRENT_BINARY_DIR}/wlr-protocols)
  file(MAKE_DIRECTORY ${NATIVE_WLR_GEN})
  add_custom_command(
    OUTPUT ${NATIVE_WLR_GEN}/xdg-shell-protocol.h
    COMMAND ${WAYLAND_SCANNER} server-header
            ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml ${NATIVE_WLR_GEN}/xdg-shell-protocol.h
    DEPENDS ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
  list(APPEND NATIVE_BACKEND_SRCS ${N}/wlr/output_wlr.c ${N}/wlr/wl_clip.c ${N}/wlr/wl_ime.c ${NATIVE_WLR_GEN}/xdg-shell-protocol.h
       # The launcher's Linux applications (L9b): started by the compositor;
       # files opened in them (L11).
       ${N}/sys/linux_apps.c ${N}/sys/linux_mime.c
       # logind's Lock and PrepareForSleep lock the screen (L13)
       ${N}/sys/linux_logind.c)
endif()
target_sources(core_objs PRIVATE ${NATIVE_BACKEND_SRCS})
target_include_directories(core_objs PRIVATE
  ${N}/backend
  ${N}/machine
  ${N}/present
  ${N}/output
  ${N}/audio
  ${N}/input
  ${N}/net
  ${N}/etc
  ${N}/sys
  ${N}/wlr
  ${FMRB_ROOT}/hosted/gate)
target_compile_definitions(core_objs PRIVATE
  FMRB_PLATFORM_NATIVE=1
  NATIVE_HAVE_DRM=${NATIVE_HAVE_DRM}
  NATIVE_HAVE_ALSA=${NATIVE_HAVE_ALSA}
  NATIVE_HAVE_WLROOTS=${NATIVE_HAVE_WLROOTS}
  # The debugger listens beside the simulation's 5555
  # (FMRB_DEBUG_TCP_PORT in the environment moves it again at run time).
  FMRB_DEBUG_TCP_PORT=5565
  # Resolved host paths are absolute directories here, not "/flash/...".
  DIR_HAL_PATH_MAX=512)
if(NATIVE_PACKAGE)
  target_compile_definitions(core_objs PRIVATE
    FMRB_NATIVE_PACKAGE=1
    FMRB_NATIVE_DATA_REL="${NATIVE_PACKAGE_DATA_REL}")
else()
  target_compile_definitions(core_objs PRIVATE
    FMRB_NATIVE_DEFAULT_SYSTEM_DIR="${FMRB_NATIVE_SYSTEM_DIR_ABS}"
    FMRB_NATIVE_DEFAULT_ETC_SEED="${FMRB_NATIVE_ETC_SEED_ABS}"
    FMRB_NATIVE_DEFAULT_MACHINES_DIR="${FMRB_NATIVE_MACHINES_DIR_ABS}")
endif()
# Only the output and sound files talk to their libraries; everything else
# stays free of them.
set_source_files_properties(${N}/output/output_sdl.c ${N}/audio/audio_sdl.c
  PROPERTIES COMPILE_OPTIONS "${SDL2_CFLAGS}")
if(NATIVE_HAVE_DRM)
  set_source_files_properties(${N}/output/output_drm.c PROPERTIES COMPILE_OPTIONS "${LIBDRM_CFLAGS}")
endif()
if(NATIVE_HAVE_ALSA)
  set_source_files_properties(${N}/audio/audio_alsa.c ${N}/sys/sys_linux_mixer.c
    PROPERTIES COMPILE_OPTIONS "${ALSA_CFLAGS}")
endif()

if(NATIVE_HAVE_WLROOTS)
  set(NATIVE_WLR_CFLAGS ${WLROOTS_CFLAGS} -I${NATIVE_WLR_GEN})
  set_source_files_properties(${N}/wlr/output_wlr.c ${N}/wlr/wl_clip.c ${N}/wlr/wl_ime.c PROPERTIES COMPILE_OPTIONS "${NATIVE_WLR_CFLAGS}")
  set_source_files_properties(${N}/sys/linux_apps.c PROPERTIES COMPILE_OPTIONS
    "-DNATIVE_HAVE_PNG=${NATIVE_HAVE_PNG};${LIBPNG_CFLAGS}")
endif()

set_source_files_properties(${N}/net/net_wrap.c PROPERTIES COMPILE_OPTIONS "${OPENSSL_CFLAGS}")

add_executable(fmruby-native $<TARGET_OBJECTS:core_objs>)
target_link_libraries(fmruby-native PRIVATE
  freertos_hosted lgfx_hosted ${FMRB_HOSTED_LIBMRUBY}
  PkgConfig::SDL2 PkgConfig::OPENSSL m pthread ${CMAKE_DL_LIBS})
# The calls that wait in the kernel, as the networking gems in libmruby make
# them, go through native/net/net_wrap.c (__wrap_X; __real_X is the library's).
# Only references from the objects linked here are redirected: SDL, ALSA and
# libssl's own internal calls are untouched.
# socket and close are wrapped to remember which task opened which socket
# (closed for a task deleted by force).
set(NATIVE_NET_WRAPPED getaddrinfo connect accept recv recvfrom send sendto select poll
    SSL_connect SSL_read SSL_write SSL_shutdown SSL_get_error socket close)
foreach(sym ${NATIVE_NET_WRAPPED})
  target_link_options(fmruby-native PRIVATE -Wl,--wrap=${sym})
endforeach()
if(NATIVE_HAVE_DRM)
  target_link_libraries(fmruby-native PRIVATE PkgConfig::LIBDRM)
endif()
if(NATIVE_HAVE_ALSA)
  target_link_libraries(fmruby-native PRIVATE PkgConfig::ALSA)
endif()
if(NATIVE_HAVE_WLROOTS)
  target_link_libraries(fmruby-native PRIVATE PkgConfig::WLROOTS)
endif()
if(NATIVE_HAVE_PNG)
  target_link_libraries(fmruby-native PRIVATE PkgConfig::LIBPNG)
endif()
target_link_options(fmruby-native PRIVATE -pthread -Wl,--gc-sections)

# The per-machine launchers of a package (APPLaunch's .desktop has a bare
# Exec, so each entry points at an executable that adds the arguments).
if(NATIVE_PACKAGE)
  foreach(entry ${NATIVE_LAUNCHERS})
    string(REPLACE "=" ";" parts "${entry}")
    list(GET parts 0 suffix)
    list(GET parts 1 machine)
    add_executable(family-mruby-${suffix} ${N}/package/launcher.c)
    target_compile_definitions(family-mruby-${suffix} PRIVATE
      FMRB_LAUNCH_PROGRAM="family-mruby" FMRB_LAUNCH_MACHINE="${machine}")
  endforeach()
endif()
