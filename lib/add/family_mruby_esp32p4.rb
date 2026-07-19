# Family mruby Modern (ESP32-P4) PicoRuby cross-build config.
# Same as family_mruby_esp32.rb but targets the RISC-V toolchain so libmruby.a
# is link-compatible with the esp32p4 firmware build (Retro/esp32s3 is Xtensa).
# -mabi=ilp32f (hard-float single) must match IDF's esp32p4 ABI for linking.
# (IDF v5.5.x uses newlib by default, so no picolibc spec is needed here.)
MRuby::CrossBuild.new("esp32p4") do |conf|
  conf.toolchain("gcc")

  conf.cc.command = "riscv32-esp-elf-gcc"
  conf.linker.command = "riscv32-esp-elf-gcc"
  conf.archiver.command = "riscv32-esp-elf-ar"

  conf.cc.host_command = "gcc"
  conf.cc.flags << "-Wall"
  conf.cc.flags << "-Wno-format"
  conf.cc.flags << "-Wno-unused-function"
  conf.cc.flags << "-Wno-maybe-uninitialized"
  # ESP32-P4 ISA/ABI. ilp32f (hard-float) is required for link compatibility
  # with the IDF esp32p4 objects. If the firmware link reports an ABI/ISA
  # mismatch, align this -march with build/toolchain/cflags of the IDF build.
  conf.cc.flags << "-march=rv32imafc_zicsr_zifencei"
  conf.cc.flags << "-mabi=ilp32f"

  conf.cc.defines << "MRB_TICK_UNIT=5"
  conf.cc.defines << "MRB_TIMESLICE_TICK_COUNT=10"
  conf.cc.defines << "MRBC_CONVERT_CRLF=1"
  conf.cc.defines << "MRB_INT64"
  conf.cc.defines << "MRB_32BIT"
  # Build-wide ABI defines: these change sizeof(mrb_state) / mrb_value, and
  # setting them only in picoruby-mruby's mrbgem.rake does not reach that gem's
  # own sources (its compiler is cloned before the mrbgem.rake body runs).
  # Keep in sync with components/picoruby-esp32/mruby_abi_defines.cmake.
  conf.cc.defines << "MRB_NO_BOXING"
  conf.cc.defines << "MRB_UTF8_STRING"
  conf.cc.defines << "MRB_CONSTRAINED_BASELINE_PROFILE=1"
  conf.cc.defines << "PICORB_ALLOC_ESTALLOC"
  conf.cc.defines << "PICORB_ALLOC_ALIGN=8"
  conf.cc.defines << "USE_FAT_FLASH_DISK"
  conf.cc.defines << "NDEBUG"
  conf.cc.defines << "ESP32_PLATFORM"
  conf.cc.defines << "FMRB_NO_IO_CONSOLE"
  # Remote debugger VM hook (doc/vm_remote_debug_*). ABI-relevant: adds two
  # function pointers to mrb_state.
  conf.cc.defines << "MRB_USE_DEBUG_HOOK"
  # Select the Modern (Tab5) section of fmrb_pin_assign.h for any rake-built
  # source that includes it. NOTE: gem port sources that use ESP-IDF headers
  # (e.g. picoruby-fmrb-const ports/esp32/const.c, which exposes
  # FmrbHw::PIN_*) are compiled by the picoruby-esp32 IDF component instead;
  # that CMakeLists.txt defines FMRB_HW_MODERN for esp32p4 as well.
  conf.cc.defines << "FMRB_HW_MODERN"

  if ENV['PICORB_DEBUG']
    conf.cc.defines << "ESTALLOC_DEBUG"
    conf.enable_debug
  end

  conf.picoruby

  # HAL port selection: on esp32 the rake build compiles NO ports at all
  # (CrossBuild default when conf.ports is unset). Every needed port requires
  # ESP-IDF headers and is compiled by the picoruby-esp32 CMake component
  # (PICORUBY_SRCS); see family_mruby_esp32.rb for details.

  # Common gems
  conf.gembox "family_mruby"

  # mruby extension gems
  dir = "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems"
  conf.gem gemdir: "#{dir}/mruby-kernel-ext"
  conf.gem gemdir: "#{dir}/mruby-string-ext"
  conf.gem gemdir: "#{dir}/mruby-array-ext"
  conf.gem gemdir: "#{dir}/mruby-time"
  conf.gem gemdir: "#{dir}/mruby-objectspace"
  conf.gem gemdir: "#{dir}/mruby-metaprog"
  conf.gem gemdir: "#{dir}/mruby-error"
  conf.gem gemdir: "#{dir}/mruby-sprintf"
  conf.gem gemdir: "#{dir}/mruby-math"
  conf.gem gemdir: "#{dir}/mruby-dir"

  # Hardware peripheral gems
  hw = "#{MRUBY_ROOT}/mrbgems"
  conf.gem gemdir: "#{hw}/picoruby-gpio"
  conf.gem gemdir: "#{hw}/picoruby-rmt"
  conf.gem gemdir: "#{hw}/picoruby-i2c"

  # RTC drivers (pure Ruby, depend on picoruby-i2c).
  # Tab5 carries an RX8130; RX8900 stays in for code shared with Retro.
  conf.gem core: "picoruby-rx8900"
  conf.gem core: "picoruby-rx8130"

  # Ruby networking client API (Modern/P4 only, see doc/ruby_network_api_design.md).
  # picoruby-socket pulls picoruby-mbedtls; ESP-IDF port sources are compiled by
  # components/picoruby-esp32/CMakeLists.txt (PICORUBY_SRCS), lib/patch applies
  # the esp32p4 build fixes to both mrbgem.rake files.
  conf.gem core: "picoruby-socket"
  conf.gem core: "picoruby-net-http"
  conf.gem core: "picoruby-net-websocket"
  # conf.gem gemdir: "#{hw}/picoruby-adc"
  # conf.gem gemdir: "#{hw}/picoruby-pwm"
  # conf.gem gemdir: "#{hw}/picoruby-spi"
  # conf.gem gemdir: "#{hw}/picoruby-uart"
  # conf.gem gemdir: "#{hw}/picoruby-watchdog"
end
