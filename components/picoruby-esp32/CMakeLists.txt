set(PICORUBY_DIR ${COMPONENT_DIR}/picoruby)

# Set platform-specific build configuration and dependencies
if(IDF_TARGET STREQUAL "linux")
    set(MRUBY_CONFIG "family_mruby_linux")  # POSIXビルド用
    set(MRUBY_BUILD_DIR "host")
    set(LIBMRUBY_FILE ${PICORUBY_DIR}/build/host/lib/libmruby.a)
    set(MRBGEMS_DIR ${PICORUBY_DIR}/build/host/mrbgems)
    set(PICORUBY_BUILD_INCLUDE ${COMPONENT_DIR}/picoruby/build/host/include)
    set(PLATFORM_PRIV_REQUIRES
        fmrb_hal
    )

    if(ENABLE_ASAN)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -fno-omit-frame-pointer")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    endif()

else()
    set(MRUBY_CONFIG "family_mruby_esp32")  # ESP32ビルド用
    set(MRUBY_BUILD_DIR "esp32")
    set(LIBMRUBY_FILE ${PICORUBY_DIR}/build/esp32/lib/libmruby.a)
    set(MRBGEMS_DIR ${PICORUBY_DIR}/build/esp32/mrbgems)
    set(PICORUBY_BUILD_INCLUDE ${COMPONENT_DIR}/picoruby/build/esp32/include)
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
        fmrb_hal
    )
endif()

# Set platform-specific source files
if(IDF_TARGET STREQUAL "linux")
    set(PICORUBY_SRCS
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/ports/posix/machine.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/ports/posix/hal.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/ports/hal.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/ports/gfx.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/ports/audio.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-filesystem-fat/ports/esp32/fat.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-filesystem-fat/ports/esp32/fat_file.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-filesystem-fat/ports/esp32/fat_dir.c

    )
else()
  set(PICORUBY_SRCS
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/ports/hal.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/ports/gfx.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-fmrb-app/ports/audio.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-filesystem-fat/ports/esp32/fat.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-filesystem-fat/ports/esp32/fat_file.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-filesystem-fat/ports/esp32/fat_dir.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/ports/esp32/machine.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-io-console/ports/esp32/io-console.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-env/ports/esp32/env.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-gpio/ports/esp32/gpio.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-adc/ports/esp32/adc.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-spi/ports/esp32/spi.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-rng/ports/esp32/rng.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-pwm/ports/esp32/pwm.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-rmt/ports/esp32/rmt.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-watchdog/ports/esp32/watchdog.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/esp32/timing_alt.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/cipher.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-i2c/ports/esp32/i2c.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-uart/ports/esp32/uart.c
    )
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
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-filesystem-fat/ports/esp32
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler2/include
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler2/lib/prism/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mruby/lib/mruby/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mruby/include
  PRIV_REQUIRES
    ${PLATFORM_PRIV_REQUIRES}
    mbedtls
)

# Add platform-specific dependencies for Linux
if(IDF_TARGET STREQUAL "linux")
  target_link_libraries(${COMPONENT_LIB} PRIVATE mbedtls)
endif()

# Add platform-specific compile definitions
if(IDF_TARGET STREQUAL "linux")
  add_compile_definitions(PICORB_PLATFORM_POSIX)
  add_compile_definitions(PICORB_VM_MRUBY=1)
  add_compile_definitions(MRB_INT64=1)
  add_compile_definitions(MRB_TICK_UNIT=5)
  add_compile_definitions(MRB_TIMESLICE_TICK_COUNT=10)
else()
  add_compile_definitions(PICORB_VM_MRUBY=1)
  add_compile_definitions(MRB_INT64=1)
  add_compile_definitions(MRB_TICK_UNIT=5)
  add_compile_definitions(MRB_TIMESLICE_TICK_COUNT=10)
  add_compile_definitions(NDEBUG)
  add_compile_definitions(USE_FAT_FLASH_DISK)
  #MRBC definitions is used by machine.
  add_definitions(
    -DMRBC_TICK_UNIT=10
    -DMRBC_TIMESLICE_TICK_COUNT=1
    -DMRBC_USE_FLOAT=2
    -DMRC_CUSTOM_ALLOC
    -DMRBC_CONVERT_CRLF=1
  )
endif()


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
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler2/include
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler2/lib/prism/include
    ${PICORUBY_BUILD_INCLUDE}
    ${MRBGEMS_DIR}
)

add_custom_target(
  picoruby DEPENDS ${LIBMRUBY_FILE}
  DEPENDS ${LIBMRUBY_FILE}
)
add_dependencies(${COMPONENT_LIB} picoruby)

# No Ruby files to compile - main application is handled by picoruby-fmrb-app mrbgem
