# Family mruby Core

Family mruby OS core firmware - A lightweight embedded OS with multi-VM support running on ESP32-S3.

## About

Family mruby Core is an embedded operating system designed for ESP32-S3-N16R8, providing:

- **Multi-VM Runtime**: Support for MicorRuby(mruby), Lua, and native C applications
- **Graphics System**: Abstracted graphics operations with LovyanGFX-compatible API
- **Task Management**: FreeRTOS-based multitasking with per-app memory isolation
- **Dual-Platform Development**: Build for ESP32 hardware or Linux (with SDL2 simulation)

## Features

- **Multi-VM Application Support**
  - mruby (MicroRuby) for Ruby applications
  - Lua 5.4 for Lua scripts

- **Graphics & Audio**
  - Controlled via abstracted API. Graphics and audio are handled by a secondary ESP32 microcontroller via SPI.

- **Development Tools**
  - Linux simulation environment with SDL2
  - USB host support (keyboard/mouse)
  - SD card filesystem access

## Build Requirements

- **Docker** - For ESP32 builds (ESP-IDF v5.5.1)
- **Ruby** - For build scripts

### Host Development Dependencies

Docker is required.

Use the Docker container built from the docker/ directory via rake commands.

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
./run_core_linux.sh
```

### Cleaning

```bash
# Clean build artifacts
rake clean
# or
rake clean_all
```

## License

See [LICENSE](LICENSE) file for details.

This project uses third-party open source software. See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for details about the licenses of dependencies.
