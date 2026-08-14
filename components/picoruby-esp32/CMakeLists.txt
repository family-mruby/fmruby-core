set(PICORUBY_DIR ${COMPONENT_DIR}/picoruby)

# Set platform-specific build configuration and dependencies
if(IDF_TARGET STREQUAL "linux")
    set(MRUBY_CONFIG "${CMAKE_SOURCE_DIR}/lib/add/family_mruby_linux.rb")
    set(MRUBY_BUILD_DIR "family-mruby-linux")
    set(LIBMRUBY_FILE ${PICORUBY_DIR}/build/family-mruby-linux/lib/libmruby.a)
    set(MRBGEMS_DIR ${PICORUBY_DIR}/build/family-mruby-linux/mrbgems)
    set(PICORUBY_BUILD_INCLUDE ${COMPONENT_DIR}/picoruby/build/family-mruby-linux/include)
    set(PLATFORM_PRIV_REQUIRES
        fmrb_hal
        fmrb_gfx
        fmrb_log
    )

    if(ENABLE_ASAN)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -fno-omit-frame-pointer")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    endif()

else()
    # libmruby.a must be built with the matching toolchain: Xtensa for the Retro
    # esp32* targets, RISC-V for the Modern esp32p4 target. Separate build dir
    # per ISA so objects never get mixed.
    if(IDF_TARGET STREQUAL "esp32p4")
        set(MRUBY_CONFIG "${CMAKE_SOURCE_DIR}/lib/add/family_mruby_esp32p4.rb")
        set(MRUBY_BUILD_DIR "esp32p4")
    else()
        set(MRUBY_CONFIG "${CMAKE_SOURCE_DIR}/lib/add/family_mruby_esp32.rb")  # ESP32ビルド用
        set(MRUBY_BUILD_DIR "esp32")
    endif()
    set(LIBMRUBY_FILE ${PICORUBY_DIR}/build/${MRUBY_BUILD_DIR}/lib/libmruby.a)
    set(MRBGEMS_DIR ${PICORUBY_DIR}/build/${MRUBY_BUILD_DIR}/mrbgems)
    set(PICORUBY_BUILD_INCLUDE ${COMPONENT_DIR}/picoruby/build/${MRUBY_BUILD_DIR}/include)
    set(PLATFORM_PRIV_REQUIRES
        freertos
        spi_flash
        esp_timer
        esp_driver_gpio
        esp_adc
        esp_driver_spi
        esp_driver_uart
        esp_driver_ledc
        esp_driver_rmt
        esp_driver_i2c
        esp_hw_support
        esp_psram
        esp_system
        lwip
        fmrb_hal
        fmrb_gfx
        fmrb_log
    )
endif()

# Set platform-specific source files
if(IDF_TARGET STREQUAL "linux")
    file(GLOB FMRB_APP_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/ports/esp32/*.c")
    file(GLOB FMRB_KERNEL_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-kernel/ports/esp32/*.c")
    file(GLOB FMRB_CONST_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-const/ports/esp32/*.c")
    file(GLOB FMRB_LOG_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-log/ports/esp32/*.c")
    file(GLOB FMRB_IO_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-io/ports/esp32/*.c")
    file(GLOB FMRB_FILESYSTEM_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-filesystem/ports/esp32/*.c")
    file(GLOB FMRB_MSGPACK_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-msgpack/ports/esp32/*.c")
    file(GLOB FMRB_BMP332_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-bmp332/ports/esp32/*.c")
    file(GLOB FMRB_FFT_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-fft/ports/esp32/*.c")
    file(GLOB FMRB_SPINEL_HELLO_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-spinel-hello/ports/esp32/*.c")
    file(GLOB FMRB_DEBUG_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-debug/ports/esp32/*.c")
    file(GLOB FMRB_EDITOR_CORE_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-editor-core/ports/esp32/*.c")
    file(GLOB FMRB_MIDI_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-midi/ports/esp32/*.c")
    set(PICORUBY_SRCS
        ${FMRB_APP_PORTS_SRCS}
        ${FMRB_KERNEL_PORTS_SRCS}
        ${FMRB_CONST_PORTS_SRCS}
        ${FMRB_LOG_PORTS_SRCS}
        ${FMRB_IO_PORTS_SRCS}
        ${FMRB_FILESYSTEM_PORTS_SRCS}
        ${FMRB_MSGPACK_PORTS_SRCS}
        ${FMRB_BMP332_PORTS_SRCS}
        ${FMRB_FFT_PORTS_SRCS}
        ${FMRB_SPINEL_HELLO_PORTS_SRCS}
        ${FMRB_DEBUG_PORTS_SRCS}
        ${FMRB_EDITOR_CORE_PORTS_SRCS}
        ${FMRB_MIDI_PORTS_SRCS}
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-bmp332/src/fmrb_bmp332.c
        # The mruby-task case-D tick (top-half) needs FreeRTOS headers, so it is
        # compiled here (this component PRIV_REQUIRES freertos) rather than by the
        # rake mruby build. A name-only hal-task-freertos gem stops mruby-task
        # from compiling its own posix (SIGALRM) port. hal_freertos.c is gone:
        # its tick manager moved into this port.
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/ports/freertos/task_hal.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/ports/common/machine.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-rng/ports/posix/rng.c
        # NOTE: picoruby-socket / picoruby-mbedtls / picoruby-machine posix port
        # sources are NOT listed here for linux: with conf.ports :posix the rake
        # build compiles each gem's ports/posix (and picoruby-machine console)
        # automatically. SSL on linux uses OpenSSL (linked below).
    )
else()
    file(GLOB FMRB_APP_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/ports/esp32/*.c")
    file(GLOB FMRB_KERNEL_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-kernel/ports/esp32/*.c")
    file(GLOB FMRB_CONST_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-const/ports/esp32/*.c")
    file(GLOB FMRB_LOG_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-log/ports/esp32/*.c")
    file(GLOB FMRB_IO_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-io/ports/esp32/*.c")
    file(GLOB FMRB_FILESYSTEM_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-filesystem/ports/esp32/*.c")
    file(GLOB FMRB_MSGPACK_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-msgpack/ports/esp32/*.c")
    file(GLOB FMRB_BMP332_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-bmp332/ports/esp32/*.c")
    file(GLOB FMRB_FFT_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-fft/ports/esp32/*.c")
    file(GLOB FMRB_SPINEL_HELLO_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-spinel-hello/ports/esp32/*.c")
    file(GLOB FMRB_DEBUG_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-debug/ports/esp32/*.c")
    file(GLOB FMRB_EDITOR_CORE_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-editor-core/ports/esp32/*.c")
    file(GLOB FMRB_MIDI_PORTS_SRCS "${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-midi/ports/esp32/*.c")

    set(PICORUBY_SRCS
        ${FMRB_APP_PORTS_SRCS}
        ${FMRB_KERNEL_PORTS_SRCS}
        ${FMRB_CONST_PORTS_SRCS}
        ${FMRB_LOG_PORTS_SRCS}
        ${FMRB_IO_PORTS_SRCS}
        ${FMRB_FILESYSTEM_PORTS_SRCS}
        ${FMRB_MSGPACK_PORTS_SRCS}
        ${FMRB_BMP332_PORTS_SRCS}
        ${FMRB_FFT_PORTS_SRCS}
        ${FMRB_SPINEL_HELLO_PORTS_SRCS}
        ${FMRB_DEBUG_PORTS_SRCS}
        ${FMRB_EDITOR_CORE_PORTS_SRCS}
        ${FMRB_MIDI_PORTS_SRCS}
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-bmp332/src/fmrb_bmp332.c
        # case-D tick top-half + mruby-task HAL (same consolidation as linux:
        # the machine esp32 port no longer carries its own tick manager)
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/ports/freertos/task_hal.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/ports/esp32/machine.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/ports/common/machine.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/src/machine.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-io-console/ports/esp32/io-console.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-env/ports/esp32/env.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/add/picoruby-gpio-proxy/gpio.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-adc/ports/esp32/adc.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-spi/ports/esp32/spi.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-rng/ports/esp32/rng.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-pwm/ports/esp32/pwm.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/add/picoruby-rmt-proxy/rmt.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-watchdog/ports/esp32/watchdog.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/esp32/timing_alt.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/cipher.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/digest.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/cmac.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/hmac.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/md.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/pkey.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/add/picoruby-i2c-proxy/i2c.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-uart/ports/esp32/uart.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-require/ports/esp32/platform.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/add/hal-esp32-dir/dir_hal.c
    )

    # Ruby networking (picoruby-socket) ESP-IDF port: every WiFi-capable
    # target -- P4 (radio on the C6) and Narya S3 (native radio). ATOM keeps
    # WiFi off, so its build config omits the gems and these sources too.
    # The gems are enabled in lib/add/family_mruby_esp32{,p4}.rb; these
    # sources use ESP-IDF LwIP (BSD sockets) and mbedTLS. tcp_socket.c is
    # patched via lib/patch/picoruby-socket (default timeouts).
    if(IDF_TARGET STREQUAL "esp32p4" OR
       (IDF_TARGET STREQUAL "esp32s3" AND NOT FMRB_HW_TARGET STREQUAL "ATOM_DISPLAY"))
        list(APPEND PICORUBY_SRCS
            ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/tcp_socket.c
            ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/udp_socket.c
            ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/tcp_server.c
            ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/ssl_socket.c
            ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/net_helpers.c
        )
    endif()
endif()

# Add a dummy source file for Linux build to avoid INTERFACE library
if(IDF_TARGET STREQUAL "linux")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.c "// Dummy file for Linux build\n")
  list(APPEND PICORUBY_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dummy.c)
endif()

idf_component_register(
  SRCS ${PICORUBY_SRCS}
  INCLUDE_DIRS
    ${COMPONENT_DIR}
    ${COMPONENT_DIR}/picoruby/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-io-console/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-const/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-bmp332/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-midi/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/ports
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-msgpack/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-debug/include
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler/include
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler/lib/prism/include
    # picoruby-ti: only the public API headers. The engine's own sources are
    # compiled by the rake mruby build (it is an ordinary mrbgem, see
    # lib/add/family_mruby.gembox), NOT here -- listing them in PICORUBY_SRCS
    # too builds all 22 of them a second time into this component's archive.
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-ti/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mruby/lib/mruby/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mruby/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-dir/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-gpio/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-i2c/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-rmt/include
  PRIV_REQUIRES
    ${PLATFORM_PRIV_REQUIRES}
    mbedtls
    msgpack-esp32
    main
)

# Add platform-specific dependencies for Linux
if(IDF_TARGET STREQUAL "linux")
  target_link_libraries(${COMPONENT_LIB} PRIVATE mbedtls)
  # OpenSSL for the picoruby-socket POSIX SSL port (libssl-dev in the container)
  target_link_libraries(${COMPONENT_LIB} PRIVATE ssl crypto)
  # Disable optimization for debugging
  # Todo: disable it on official release
  target_compile_options(${COMPONENT_LIB} PRIVATE -O0 -g3)
endif()

# Add platform-specific compile definitions.
# All layout-affecting MRB_* macros come from mruby_abi_defines.cmake and must
# match the rake-built libmruby exactly (see that file for the sync rules).
include(${CMAKE_CURRENT_LIST_DIR}/mruby_abi_defines.cmake)
fmrb_add_mruby_abi_defines()

if(IDF_TARGET STREQUAL "linux")
  add_compile_definitions(PICORB_PLATFORM_POSIX)
  add_compile_definitions(PICORB_VM_MRUBY=1)
else()
  add_compile_definitions(PICORB_VM_MRUBY=1)
  add_compile_definitions(NDEBUG)
  #MRBC definitions is used by machine.
  add_definitions(
    -DMRBC_TICK_UNIT=10
    -DMRBC_TIMESLICE_TICK_COUNT=1
    -DMRBC_USE_FLOAT=2
    -DMRC_CUSTOM_ALLOC
    -DMRBC_CONVERT_CRLF=1
  )
endif()

# Select the board section of fmrb_pin_assign.h for gem port sources compiled
# by this component (e.g. picoruby-fmrb-const const.c, which exposes
# FmrbHw::PIN_* to Ruby). The macros are PRIVATE per component, so the shared
# helper must be called here too or the constants silently fall back to Retro
# pins.
include(${CMAKE_CURRENT_LIST_DIR}/../fmrb_common/fmrb_hw_defines.cmake)
fmrb_add_hw_defines()


# Create build directories to avoid CMake validation errors
execute_process(
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PICORUBY_BUILD_INCLUDE}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${MRBGEMS_DIR}
)

# Build PicoRuby
add_custom_command(
  OUTPUT ${LIBMRUBY_FILE}
  COMMAND ${CMAKE_COMMAND} -E echo "MRUBY_CONFIG=${MRUBY_CONFIG} rake"
  COMMAND ${CMAKE_COMMAND} -E env MRUBY_CONFIG=${MRUBY_CONFIG} rake
  WORKING_DIRECTORY ${PICORUBY_DIR}
  COMMENT "PicoRuby build (${MRUBY_CONFIG})"
  VERBATIM
)

add_prebuilt_library(
  libmruby ${LIBMRUBY_FILE}
  REQUIRES ${COMPONENT_NAME}
)
target_link_libraries(${COMPONENT_LIB} PRIVATE libmruby)
target_include_directories(
  ${COMPONENT_LIB}
  PUBLIC
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler/include
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler/lib/prism/include
    ${PICORUBY_BUILD_INCLUDE}
    ${MRBGEMS_DIR}
)

add_custom_target(
  picoruby DEPENDS ${LIBMRUBY_FILE}
  DEPENDS ${LIBMRUBY_FILE}
)
add_dependencies(${COMPONENT_LIB} picoruby)

# No Ruby files to compile - main application is handled by picoruby-fmrb-app mrbgem
