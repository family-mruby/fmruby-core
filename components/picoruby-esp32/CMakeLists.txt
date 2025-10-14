set(PICORUBY_DIR ${COMPONENT_DIR}/picoruby)

# Set platform-specific build configuration
if(IDF_TARGET STREQUAL "linux")
    set(MRUBY_CONFIG "family_mruby_linux")  # POSIXビルド用
    set(MRUBY_BUILD_DIR "host")
    set(LIBMRUBY_FILE ${PICORUBY_DIR}/build/host/lib/libmruby.a)
    set(MRBGEMS_DIR ${PICORUBY_DIR}/build/host/mrbgems)
    set(PICORUBY_BUILD_INCLUDE ${COMPONENT_DIR}/picoruby/build/host/include)
else()
    set(MRUBY_CONFIG "family_mruby_esp32")  # ESP32ビルド用
    set(MRUBY_BUILD_DIR "esp32")
    set(LIBMRUBY_FILE ${PICORUBY_DIR}/build/esp32/lib/libmruby.a)
    set(MRBGEMS_DIR ${PICORUBY_DIR}/build/esp32/mrbgems)
    set(PICORUBY_BUILD_INCLUDE ${COMPONENT_DIR}/picoruby/build/esp32/include)
endif()

# Set platform-specific source files
if(IDF_TARGET STREQUAL "linux")
    set(PICORUBY_SRCS
        ${COMPONENT_DIR}/picoruby-esp32.c
    )
else()
    set(PICORUBY_SRCS
        ${COMPONENT_DIR}/picoruby-esp32.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/ports/esp32/machine.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-io-console/ports/esp32/io-console.c
        ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-filesystem-fat/ports/esp32/flash_disk.c
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
  PRIV_REQUIRES
)

# Add platform-specific dependencies
if(IDF_TARGET STREQUAL "linux")
  target_link_libraries(${COMPONENT_LIB} PRIVATE
    mbedtls
  )
else()
  target_link_libraries(${COMPONENT_LIB} PRIVATE
    esp_driver_gpio
    esp_driver_uart
    esp_driver_spi
    esp_driver_ledc
    esp_driver_rmt
    esp_driver_i2c
    esp_adc
    esp_timer
    esp_hw_support
    spi_flash
    mbedtls
  )
endif()

add_definitions(
  -DMRBC_TICK_UNIT=10
  -DMRBC_TIMESLICE_TICK_COUNT=1
  -DMRBC_USE_FLOAT=2
  -DMRC_CUSTOM_ALLOC
  -DMRBC_CONVERT_CRLF=1
  -DUSE_FAT_FLASH_DISK
  -DNDEBUG

  # for picoruby.h
  #-DPICORB_VM_MRUBYC
  -DPICORB_VM_MRUBY
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
  PRIVATE
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

# Compile Ruby files
set(RUBY_FILES main_task)
set(PICORBC ${COMPONENT_DIR}/picoruby/bin/picorbc)
set(GENERATED_C_FILES "")

foreach(rb ${RUBY_FILES})
  set(C_FILE ${COMPONENT_DIR}/mrb/${rb}.c)
  set(RB_FILE ${COMPONENT_DIR}/mrblib/${rb}.rb)
  add_custom_command(
    OUTPUT ${C_FILE}
    COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${RB_FILE}..."
    COMMAND ${CMAKE_COMMAND} -E make_directory ${COMPONENT_DIR}/mrb
    COMMAND ${PICORBC} -B${rb} -o${C_FILE} ${RB_FILE}
    DEPENDS picoruby ${RB_FILE}
    COMMENT "Compiling ${RB_FILE}"
    VERBATIM
  )
  list(APPEND GENERATED_C_FILES ${C_FILE})
endforeach(rb)

target_sources(${COMPONENT_LIB} PRIVATE ${GENERATED_C_FILES})
