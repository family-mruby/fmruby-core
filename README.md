# Family mruby Core

Family mruby OS core firmware — a lightweight embedded OS with multi-VM support running on ESP32-S3.

## About

Family mruby Core is an embedded operating system designed for ESP32-S3 (default target: N16R8). Graphics (NTSC-J) and audio (APU-emulated I2S) are delegated to a companion ESP32 running [fmruby-graphics-audio](../fmruby-graphics-audio/) over a serial link.

- **Multi-VM Runtime**: Run mruby (MicroRuby), Lua 5.4, and native C applications concurrently, each with its own isolated heap
- **Graphics System**: Abstracted graphics API routed to the companion MCU
- **Task Management**: FreeRTOS-based multitasking with per-app memory pools
- **Dual-Platform Development**: Build for ESP32 hardware or for Linux as a simulation target

## Features

- **Multi-VM application support**
  - mruby (MicroRuby) with picoruby mrbgems
  - Lua 5.4
  - Native C apps
- **Graphics & Audio**
  - Controlled via an abstracted API. Actual NTSC output and APU-based audio run on the companion ESP32, reached over UART (default, 921600 bps) with an optional SPI transport.
- **Input**
  - USB host keyboard / mouse / gamepad (HID)
  - I2C keyboard driver
- **Storage**
  - SD card and on-chip littleFS filesystems
- **Development**
  - Linux simulation using the SDL2 host shipped with fmruby-graphics-audio

## Build Requirements

- **Docker** — the ESP-IDF v5.5.1 toolchain image (`ghcr.io/family-mruby/fmruby-esp32-build:latest`) is pulled on first build. A local build of the image is also available via [docker/](docker/).
- **Ruby** — the build orchestrator is a `Rakefile`. For flashing, install `serialport`:
  ```bash
  gem install serialport
  ```

## Quick Start

### Preparing the Docker image (optional — built image is fetched from GHCR by default)

```bash
cd docker
./build.sh
```

### Building

```bash
# ESP32-S3 target (default HW: N16R8)
rake build:esp32

# ESP32-S3 target for ATOM Display (N8R8)
FMRB_HW_TARGET=ATOM_DISPLAY rake build:esp32

# Use SPI link instead of the default UART
CMAKE_OPTS="-DFMRB_LINK_TRANSPORT=SPI" rake build:esp32

# Linux target (simulation — requires fmruby-graphics-audio/sdl2-display)
rake build:linux

# List all available tasks
rake -T
```

### Flashing and running on ESP32

```bash
rake check-port   # detect & cache the serial port once
rake flash        # flash the current build
rake monitor      # open idf.py monitor
```

### Running the Linux simulation

The Linux simulation relies on the SDL2 host process shipped with [fmruby-graphics-audio](../fmruby-graphics-audio/sdl2-display/). Start the SDL2 display and `fmruby-graphics-audio.elf` before launching the core:

```bash
./build/fmruby-core.elf
```

See the repository root [README.md](../README.md) for a one-shot `docker compose up` integration path.

### Configuration and firmware content

- HW-specific defaults live under [config/](config/) (`sdkconfig.defaults.*`, `system_conf_*.toml`, `partitions_*.csv`).
- Ruby / app / system files shipped to the device come from [flash/](flash/) (apps under `flash/app/`, home scripts under `flash/home/`).

### Cleaning

```bash
rake clean       # clean picoruby incremental artifacts
rake clean_all   # full clean (forces target reconfigure)
```

Run `rake clean` after editing anything under `lib/` (mrbgems/patches are re-copied on build), and `rake clean_all` whenever switching target between `linux` and `esp32`.

## License

See the [LICENSE](LICENSE) file for details.

This project uses third-party open source software. See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for dependency licenses.
