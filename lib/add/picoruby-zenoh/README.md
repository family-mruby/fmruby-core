# picoruby-zenoh

A thin Ruby layer over [zenoh-pico](https://github.com/eclipse-zenoh/zenoh-pico):
open a client session to a Zenoh router, `put` values and `subscribe` to keys.
The Ruby name is `Zenoh`.

```ruby
s = Zenoh::Session.open("tcp/192.168.1.10:7447")   # client mode
sub = s.subscribe("demo/in")
loop do
  s.poll                                  # run zenoh-pico's pending work
  s.put("demo/out", "hello")
  sub.each_pending { |key, payload| puts "#{key}: #{payload}" }
  sleep_ms 50
end
```

## API

| Call | Returns | Notes |
|---|---|---|
| `Zenoh::Session.open(locator)` | `Session` | Client mode, connects to `locator` (`tcp/host:port`). Raises `Zenoh::Error` when the router cannot be reached. Blocks while connecting (a few seconds at most). |
| `session.put(key, payload)` | `nil` | `payload` is a String (bytes, sent as is). `ArgumentError` on a bad key, `Zenoh::Error` when the session is closed or the put fails. |
| `session.subscribe(key, depth = 16)` | `Subscriber` | `key` may be a key expression (`demo/**`). Up to `depth` received values are kept until read. |
| `session.poll(steps = 8)` | `true` / `false` | Reads the socket and runs keep-alive / lease work, at most `steps` times. Never blocks. `false` once the session has closed (router gone, lease expired). |
| `session.closed?` | `true` / `false` | |
| `session.close` | `nil` | Closes the subscribers too. Idempotent. Optional (see below). |
| `sub.each_pending { \|key, payload\| }` | Integer | Takes out the values received so far (oldest first). Without a block, returns them as `[[key, payload], ...]`. |
| `sub.pending` / `sub.received` / `sub.dropped` | Integer | Waiting values / total received / dropped because the ring was full (the oldest goes). |
| `sub.close` / `sub.closed?` | | Pending values can still be taken after close. |
| `Zenoh::PICO_VERSION` | String | zenoh-pico version compiled in. |

## Design

- **Single-threaded, polled.** zenoh-pico is built with
  `Z_FEATURE_MULTI_THREAD=0`; nothing runs behind the interpreter. Call
  `poll` regularly (every few hundred ms at least: the router drops a client
  it has not heard from for the lease time, 10 s).
- **The receive callback does not touch the VM.** It copies key and payload
  into a bounded ring; `each_pending` makes the Ruby strings.
- **Memory**: the gem's own structures use the mruby allocator (`mrb_malloc`),
  zenoh-pico and the received values use zenoh-pico's allocator (`z_malloc`).
  The gem does not depend on any host-specific allocator so that it builds
  with plain PicoRuby / mruby. On ESP-IDF, `z_malloc` takes external RAM
  (PSRAM) only (`ports/esp32/zp_system_esp32.c`).
- **Cleanup**: `close` is optional. Garbage-collecting (or closing the VM
  with) a `Session` or `Subscriber` closes the zenoh-pico side, in either
  order.
- **Build options**: `include/zenoh_generic_config.h` (client, TCP only, no
  serial / TLS / UDP / scouting, put + subscribe only). zenoh-pico's own
  TCP links are replaced by `src/zp_tcp_posix.c` and
  `ports/esp32/zp_tcp_esp32.c` (non-blocking read for polling, connect time
  limit, bounded handshake read; retries on `EINTR`).

## Building

`mrbgem.rake` compiles the zenoh-pico sources found in `$ZENOH_PICO_DIR`, or
else in `vendor/zenoh-pico/` inside the gem (`src/` and `include/` of a
release). In Family mruby, `rake zenoh:setup` fetches the release pinned in
`lib/add/ZENOH_PICO_PIN` and `rake setup` puts it in place.

Platforms (`ZENOH_PICO_PLATFORM` overrides the choice):

- **POSIX** (default; `ZENOH_LINUX`, `ZENOH_MACOS`, `ZENOH_BSD`): everything
  is compiled by the mruby build.
- **ESP-IDF** (`ZENOH_ESPIDF`, chosen when the build name starts with
  `esp32`): the mruby build compiles the gem and the zenoh-pico core with the
  platform types of `include/zenoh_espidf_platform.h` (no ESP-IDF headers
  needed). The ESP-IDF component must compile, like other gems' `ports/esp32`:
  - `ports/esp32/zp_system_esp32.c` (zenoh-pico's `src/system/espidf/system.c`
    with a PSRAM allocator)
  - `ports/esp32/zp_tcp_esp32.c`
  - `vendor/zenoh-pico/src/system/socket/esp32.c`

  with the defines `ZENOH_GENERIC ZENOH_ESPIDF ZENOH_C_STANDARD=11
  ZENOH_COMPILER_GCC ZENOH_LOG_ERROR`, `-std=gnu11`, and the include paths
  `include/`, `<gem build dir>/zp_include` (the generated `config.h`),
  `vendor/zenoh-pico/include` and `vendor/zenoh-pico/src`, plus lwIP.
