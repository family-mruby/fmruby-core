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

- **Docker** — the ESP-IDF toolchain image is pulled on first build. The tag is pinned in the `Rakefile` (`ESP_IDF_VERSION`, currently `v5.5.4`) rather than floating on `:latest`, so that two machines building the same commit get the same toolchain. A local build of the image is also available via [docker/](docker/).
- **Ruby** — the build orchestrator is a `Rakefile`, and two build steps run on the host rather than in the container: the Spinel AOT compiler (see [below](#spinel-aot-compiler)) and the editor's type database, which needs the `rbs` gem. For flashing, install `serialport` as well:
  ```bash
  gem install rbs
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

### Spinel AOT compiler

Part of the firmware is Ruby compiled ahead of time to C by [Spinel](https://github.com/kishima/spinel). The compiler runs on the **host** (the IDF container has no Ruby set up for it), so the build needs a working copy of it — always, not only when an engine is set to `spinel`.

`rake build:*` provisions it automatically on a fresh clone: it clones the commit pinned in `components/fmrb_spinel_rt/SPINEL_PIN` into `vendor/spinel` and builds it. After that the binary is **never rebuilt on its own** — the build only checks that `vendor/spinel/bin/spinel` exists, and neither `rake clean` nor `rake clean_all` touches `vendor/`. Run the setup task explicitly when the pin moves, or as the first step of a clean build:

```bash
rake spinel:setup   # fetch + build the pinned compiler (no-op when up to date)
rake clean_all
rake build:esp32
```

`spinel:setup` compares `vendor/spinel/.built_commit` with the checkout's HEAD and rebuilds only on a mismatch. Set `SPINEL_DIR=/path/to/checkout` to build against your own working copy instead of the pinned one.

### Flashing and running on ESP32

```bash
rake check-port   # detect & cache the serial port once
rake flash        # flash the current build
rake monitor      # open idf.py monitor
```

### Running the Linux simulation

See the repository root [README.md](https://github.com/family-mruby/family-mruby/blob/main/README.md) for a one-shot `docker compose up` integration path.

### Cleaning

```bash
rake clean       # clean picoruby incremental artifacts
rake clean_all   # full clean (forces target reconfigure)
```

Run `rake clean` after editing anything under `lib/` (mrbgems/patches are re-copied on build), and `rake clean_all` whenever switching target between `linux` and `esp32`.

Neither task touches `vendor/`, so the Spinel compiler survives a full clean — add `rake spinel:setup` in front when you want the host tooling rebuilt as well (see [Spinel AOT compiler](#spinel-aot-compiler)).

## Security Note

The networking features (WiFi / BLE) have **no access control**. They are meant for
development on a trusted local network.

- **Remote desktop (HTTP / WebSocket)** — unauthenticated. Any client on the same
  network can view the screen and send keyboard / mouse input, i.e. fully operate
  the device.
- **Development remote control (`/app/launch`, `/app/kill`, `/app/list`)** —
  unauthenticated, and for development only. Present when the firmware is built
  with `FMRB_DEV_REMOTE_CTL` (the default; build a release image with
  `CMAKE_OPTS="-DFMRB_DEV_REMOTE_CTL=OFF"`). It starts and stops apps by path,
  which the remote desktop above already allows through the launcher — this is
  the scriptable form of it. See `doc/dev_remote_ctl/plan.md`.
- **BLE debug service** — no pairing, bonding or encryption (deliberately disabled
  for Web Bluetooth compatibility). Any client in radio range can connect, inspect
  running applications and their variables, and spawn or kill them.
- **WiFi credentials** are stored in plain text on the device (`config/wifi.toml`,
  deployed into the filesystem image).

Use these features only on a network you trust, at your own risk, and never expose
the device directly to the Internet.

## License

See the [LICENSE](LICENSE) file for details.

This project uses third-party open source software. See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for dependency licenses.

Creative assets bundled with this repository ship under their own licenses (CC0 / CC BY) — see [ASSETS.md](ASSETS.md).
