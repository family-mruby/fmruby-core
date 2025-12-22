# FMRuby Core

Family mruby OS core firmware - A lightweight embedded OS with multi-VM support (mruby/Lua) running on ESP32-S3.

## About

FMRuby Core is an embedded operating system designed for ESP32-S3-N16R8, providing:

- **Multi-VM Runtime**: Support for mruby, Lua, and native C applications
- **Graphics System**: Hardware-accelerated graphics with LovyanGFX-compatible API
- **Task Management**: FreeRTOS-based multitasking with per-app memory isolation
- **Dual-Platform Development**: Build for ESP32 hardware or Linux (with SDL2 simulation)

## Features

- **Multi-VM Application Support**
  - mruby (PicoRuby) for Ruby applications
  - Lua 5.4 for Lua scripts
  - Native C function execution

- **Graphics & Audio**
  - Canvas-based graphics rendering
  - NTSC video output (via SPI to separate ESP32-WROVER)
  - APU-emulated audio system

- **Development Tools**
  - Linux simulation environment with SDL2
  - USB host support (keyboard/mouse)
  - SD card filesystem access

## Build Requirements

- **Docker** - For ESP32 builds (ESP-IDF v5.5.1)
- **Ruby** - For build scripts

### Host Development Dependencies

For Linux target builds and simulation:

```bash
# Ubuntu/Debian
sudo apt-get install libsdl2-dev libmsgpack-dev pkg-config clang-format clang-tidy llvm

# macOS
brew install sdl2 msgpack pkg-config llvm
```

### Ruby Gems

```bash
gem install serialport
```

## Quick Start

### Building

```bash
# Linux target (simulation)
rake build:linux

# ESP32 target
rake build:esp32

# Show all available commands
rake -T
```

### Running

```bash
# Linux simulation
./build/fmruby-core.elf

# In another terminal, start the SDL2 host process
# (automatically started by the core in most cases)
```

### Cleaning

```bash
# Clean build artifacts
rake clean

# Note: Switch between linux/ESP32 targets requires clean build
```

## Project Structure

```
components/          ESP-IDF components
  ├── lua/          Lua 5.4 VM and extensions
  ├── picoruby-esp32/ mruby VM (submodule)
  ├── fmrb_*/       FMRuby core libraries (HAL, GFX, memory, messaging)
  └── ...
main/               FMRuby OS kernel and application layer
flash/              Flash filesystem contents (apps, configs)
host/               PC simulation environment (SDL2)
lib/                Custom mrbgems and patches
doc/                Documentation
```

## Documentation Generation

### C/C++ API Documentation

Generate API documentation using Doxygen:

```bash
# Install Doxygen
sudo apt-get install doxygen  # Ubuntu/Debian
brew install doxygen          # macOS

# Generate documentation
rake doc:c

# Open generated docs
open doc/api/html/index.html      # macOS
xdg-open doc/api/html/index.html  # Linux
```

### Ruby API Documentation

Generate Ruby API documentation using YARD:

```bash
# Install YARD
gem install yard

# Generate documentation
rake doc:ruby

# Open generated docs
open doc/ruby_api/index.html      # macOS
xdg-open doc/ruby_api/index.html  # Linux
```

### Generate All Documentation

```bash
rake doc:all
```

### Clean Documentation

```bash
rake doc:clean
```

## Development

For detailed development guidelines, architecture, and coding standards, see [CLAUDE.md](CLAUDE.md).

### Platform-Specific Notes

- **ESP32 Target**: Requires ESP-IDF v5.5.1 (via Docker container)
- **Linux Target**: Simulates hardware using POSIX APIs and SDL2
- **Submodules**: Do not modify files in submodule directories directly (see CLAUDE.md)

### Troubleshooting

Kill hanging processes and clean up sockets:

```bash
killall -9 fmrb_host_sdl2 2>/dev/null; sleep 1; rm -f /tmp/fmrb_socket
```

## License

See LICENSE file for details.
