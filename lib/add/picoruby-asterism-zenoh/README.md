# picoruby-asterism-zenoh

A thin Ruby layer over [zenoh-pico](https://github.com/eclipse-zenoh/zenoh-pico):
open a session (client of a Zenoh router, or peer), `put` values and
`subscribe` to keys, ask and answer queries (`get` / `queryable`), and announce
and watch liveliness. The Ruby name is `Asterism::Zenoh`
(no top-level constant is defined).

```ruby
s = Asterism::Zenoh::Session.open("tcp/192.168.1.10:7447")   # client mode
sub = s.subscribe("demo/in")
loop do
  s.poll                                  # run zenoh-pico's pending work
  s.put("demo/out", "hello")
  sub.each_pending { |key, payload| puts "#{key}: #{payload}" }
  sleep_ms 50
end
```

Query and reply, liveliness (all polled the same way):

```ruby
qa = s.queryable("demo/node/a/**")          # answer queries
tok = s.liveliness("demo/alive/a")          # "a is alive" while held
w = s.liveliness_watch("demo/alive/**")     # who appears / goes away
g = s.get("demo/node/b/info", 2000)         # ask; returns at once
loop do
  s.poll
  qa.each_pending { |q| q.reply(q.key, "fine") }   # finished after the block
  w.each_pending { |key, alive| puts "#{key} #{alive ? 'up' : 'down'}" }
  g.each_reply { |key, payload| puts "#{key}: #{payload}" }
  break if g.done?
  sleep_ms 50
end
```

## API

| Call | Returns | Notes |
|---|---|---|
| `Asterism::Zenoh::Session.open(locator)` | `Session` | Client mode, connects to the router at `locator` (`tcp/host:port`). Raises `Asterism::Zenoh::Error` when the router cannot be reached. Blocks while connecting (a few seconds at most). |
| `Asterism::Zenoh::Session.open(locator, mode: :peer)` | `Session` | Peer mode without a router: connects to the peer at `locator`. |
| `Asterism::Zenoh::Session.open(nil, mode: :peer, listen: "tcp/0.0.0.0:7447")` | `Session` | Peer mode, listening for peers (a `locator` may be given too). New peers are accepted by `poll` (checked about once a second). |
| `session.peers` | Integer | Connected peers (peer mode), or 1 for the router of a client session; 0 once closed. |
| `session.put(key, payload)` | `nil` | `payload` is a String (bytes, sent as is). `ArgumentError` on a bad key, `Asterism::Zenoh::Error` when the session is closed, the put fails, or the connection is found lost (see below). Waits at most `SEND_TIMEOUT_MS` for room to send. |
| `session.subscribe(key, depth = 16)` | `Subscriber` | `key` may be a key expression (`demo/**`). Up to `depth` received values are kept until read. |
| `session.poll(steps = 8)` | `true` / `false` | Reads the socket and runs keep-alive / lease work, at most `steps` times. Does not wait for data. `false` once the session has closed (closed by the app, or the connection was lost: see below). |
| `session.closed?` | `true` / `false` | Also notices a lost connection. |
| `session.close` | `nil` | Closes the subscribers too. Idempotent. Optional (see below). |
| `sub.each_pending { \|key, payload\| }` | Integer | Takes out the values received so far (oldest first). Without a block, returns them as `[[key, payload], ...]`. |
| `sub.pending` / `sub.received` / `sub.dropped` | Integer | Waiting values / total received / dropped because the ring was full (the oldest goes). |
| `sub.close` / `sub.closed?` | | Pending values can still be taken after close. |
| `session.get(key, timeout_ms = 2000, params = nil, payload = nil)` | `Get` | Sends a query and returns at once. Every matching queryable is asked (target ALL) and every reply is kept (no consolidation). `timeout_ms` 1..600000. |
| `get.each_reply { \|key, payload\| }` | Integer | Replies received so far (oldest first); without a block, an Array. Error replies are not yielded, only counted. |
| `get.done?` | `true` / `false` | True once every replier has finished, the time limit has passed, or the session closed. The limit is checked once a second by `poll`, so `done?` turns true up to about 1 s after it. |
| `get.pending` / `received` / `dropped` / `errors` | Integer | Up to 16 replies are kept; more drop the oldest. |
| `session.queryable(key, depth = 16)` | `Queryable` | Answers queries matching `key`. Up to `depth` unanswered queries are kept; more finish the oldest unanswered (the requester gets nothing from it) and count it as dropped. |
| `queryable.each_pending { \|q\| }` | Integer | Takes out the waiting queries. Each is finished when the block returns (also when it raises). Without a block: an Array of `Query`, each open until `q.finish` or garbage collection. |
| `queryable.pending` / `received` / `dropped` / `close` / `closed?` | | |
| `q.key` / `q.params` / `q.payload` | String | The query's key expression (may contain wildcards), its parameters (`a=1;b=2`) and payload (`""` when none). |
| `q.reply(payload)` / `q.reply(key, payload)` | `nil` | `key` defaults to the query's key and must match it. May be called several times. `Asterism::Zenoh::Error` once finished. |
| `q.finish` / `q.finished?` | | Sends the final reply: the requester's `done?` turns true when every queryable has finished. |
| `session.liveliness(key)` | `LivelinessToken` | Announces `key` as alive until `token.close`, garbage collection, or the session closing. |
| `session.liveliness_watch(key, depth = 16)` | `LivelinessWatch` | `each_pending { \|key, alive\| }` (alive is `true` when a token appeared, `false` when it went away); the tokens alive when the watch starts come first. Tokens of the same session are not reported (zenoh-pico does not report its own). Also `pending` / `received` / `dropped` / `close` / `closed?`. |
| `session.liveliness_get(key, timeout_ms = 2000)` | `Get` | The tokens alive now, as replies (empty payload). |
| `Asterism::Zenoh::PICO_VERSION` | String | zenoh-pico version compiled in. |
| `Asterism::Zenoh::CONNECT_TIMEOUT_MS` / `Asterism::Zenoh::SEND_TIMEOUT_MS` | Integer | The link's time limits (3000 each by default; build-time defines `PICORUBY_ZENOH_CONNECT_TIMEOUT_MS` / `PICORUBY_ZENOH_SEND_TIMEOUT_MS`). |

## Peer mode

Two machines can talk without a router: one listens, the other connects.
Everything above works the same between them (put / subscribe, get /
queryable, liveliness). Limits of zenoh-pico's peer mode: one listening
socket per session, at most 10 connected peers (`Z_LISTEN_MAX_CONNECTION_NB`,
and on ESP-IDF the lwIP socket count), and a peer does not forward between
the peers connected to it (no routing; peers that must see each other
connect to each other).

A session that only connects is closed, like a client, when its peer goes
away (closed the connection, failed, or went silent past the lease). A
listening session stays open while peers come and go.

## When the router is lost

The session is closed by the gem, and stays closed, when its TCP connection
can no longer carry it:

- the router closed the connection (it stopped, or its host dropped it), or
  the socket failed: noticed by the next `poll`, `put` or `closed?`;
- the router went silent past the lease (10 s) while the TCP connection
  stayed up: zenoh-pico drops the transport, noticed by the next `poll`,
  `put` or `closed?`;
- a send could not finish within `SEND_TIMEOUT_MS` (the router or the
  network stopped taking data and the send buffer is full): that `put`
  raises after the time limit. A half-sent message would corrupt the stream,
  so the connection is shut down rather than retried.

From then on `poll` returns `false`, `closed?` is `true` and `put` raises
`Asterism::Zenoh::Error`. Values already received can still be taken from the
subscribers. There is no automatic reconnection: to go on, the application
opens a new session (`Asterism::Zenoh::Session.open` again).

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
- **Queries are kept, not answered in the callback.** The queryable's
  callback clones the query (`z_query_clone`) into a ring; the final reply
  goes out when the last reference is dropped (after `each_pending`'s block,
  or `q.finish`). A `Get` is owned jointly by its Ruby object and zenoh-pico,
  so a `Get` collected before its replies arrive is safe.
- **Build options**: `include/zenoh_generic_config.h` (client or unicast
  peer, TCP only, no serial / TLS / UDP / scouting; put, subscribe, query,
  queryable, liveliness).
- **Peer mode is polled too.** zenoh-pico accepts peers from its cooperative
  runtime (a non-blocking accept retried every second) and waits on the peer
  sockets with `select`. The gem compiles zenoh-pico's
  `transport/unicast/read.c` with `Z_CONFIG_SOCKET_TIMEOUT=0` so that wait is
  a readiness check (otherwise every `poll` step would wait 100 ms), and on
  POSIX replaces `system/unix/network.c` with `src/zp_network_posix.c`
  (retries `select` on `EINTR`, which would otherwise stop the read task).
- **TCP links**: zenoh-pico's own are replaced by `src/zp_tcp_posix.c` and
  `ports/esp32/zp_tcp_esp32.c` (non-blocking read for polling, connect and
  send time limits, bounded handshake read, quiet non-blocking accept;
  retries on `EINTR`).

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
