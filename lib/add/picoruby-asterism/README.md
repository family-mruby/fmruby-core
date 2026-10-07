# picoruby-asterism

Ruby objects on other machines, called like local ones. Pure Ruby (mrblib
only) on top of [`Asterism::Zenoh`](../picoruby-asterism-zenoh/README.md)
and a `MessagePack` module with `pack` / `unpack` (the API of the msgpack
gem; whichever gem provides it). When PicoRuby's `Machine` is there it is
used for the clock and the short pauses while waiting; otherwise `Time` and
`sleep`.

```ruby
# on the machine that has the object
Asterism.connect("tcp/192.168.10.2:7447", node: "fmruby-90bce8", app: "demo")
Asterism.expose("apu", apu, methods: [:play, :stop])
loop { Asterism.poll; ... }               # in the update loop

# on another machine
Asterism.connect("tcp/192.168.10.2:7447", node: "linux", app: "demo")
apu = Asterism["fmruby-90bce8/demo/apu"]  # <node>/<app>/<object>
apu.play("t120 o4 cdefg")                 # runs there, returns its value
apu.respond_to?(:play)                    # => true (from the exposed list)
f = apu.async.play("cde")                 # does not wait
f.done?; f.value
Asterism.each("*/*/apu") { |a| a.stop }   # every apu alive now
Asterism.nodes                            # => ["linux", "fmruby-90bce8"]
```

## API

| Call | Returns | Raises / notes |
|---|---|---|
| `Asterism.connect(locator, node:, app:, mode: nil, listen: nil)` | `Asterism` | `locator`, `mode:`, `listen:` go to `Asterism::Zenoh::Session.open` (client of a router by default; `mode: :peer` with or without `listen:` without one). `node:` is this machine's ID, `app:` the application's name (one key chunk each: no `/ * $ ? #`, not starting with `@`; else `ArgumentError`). `Disconnected` when it cannot connect; `Error` when objects of the same `<node>/<app>` are already alive (a second copy of the application), or when already connected. Waits up to about 1.5 s for that check (a router answers at once). |
| `Asterism.expose(name, obj, methods:)` | `"<node>/<app>/<name>"` | `methods:` is an Array of names, or a Hash `name => number of arguments` (checked before the call; `-1` or the Array form: any). `ArgumentError` when `obj` has no such public method. Exposing a name again replaces it. |
| `Asterism.unexpose(name)` | true / false | |
| `Asterism.poll` | true / false | Call from the update loop: polls Zenoh, answers the calls that came in, follows who is alive. `false` once the connection is closed or lost. |
| `Asterism[path, timeout_ms = 2000]` | `Proxy` | `path` is `<node>/<app>/<object>` without wildcards (`ArgumentError`). Nothing is sent until a method is called. |
| `proxy.<method>(*args, **kw)` | the remote return value | Waits for the answer (`timeout_ms`). `RemoteError` when it raised there or could not be called (not exposed: `NoMethodError`; wrong number of arguments: `ArgumentError`; no such object: `NameError`), `Timeout` when no answer came in time (also at once when nobody answers that key), `Disconnected`, `EncodeError` (before anything is sent) for a value MessagePack cannot carry. A block cannot be sent (`ArgumentError`). |
| `proxy.async.<method>(...)` | `Future` | Sends and returns at once. `EncodeError` / `Disconnected` are raised here. |
| `future.done?` | true / false | Never waits (`Asterism.poll` moves it on). True once the answer came, the time ran out or the connection closed. |
| `future.value` | the return value | Waits (polling) if not done; raises like a waiting call. `future.took_ms`: time to the answer. |
| `proxy.respond_to?(name)` / `proxy.methods` | true / false, `[Symbol]` | From the object's meta (fetched once, `proxy.asterism_refresh` forgets it). `respond_to?` is false when the object does not answer; `methods` raises then. |
| `proxy.asterism_meta` / `proxy.asterism_path` | Hash, String | `{"methods" => [[name, arity], ...]}` |
| `Asterism.each(pattern = "**") { \|proxy\| }` | count (Array without a block) | The exposed objects alive now (this application's own included) whose `<node>/<app>/<object>` matches; `*` is one chunk, `**` any number. |
| `Asterism.nodes` | `[String]` | Node IDs alive now, this one first. |
| `Asterism.connected?` / `node_id` / `app` / `exposed` / `lost_reason` | | |
| `Asterism.close` | nil | Withdraws every exposed object. Idempotent. |

Errors: `Asterism::Error` (base, a `StandardError`), `EncodeError`, `RemoteError`
(`remote_class`, `remote_message`), `Timeout`, `Disconnected`.

### When the connection is lost

Asterism follows `Asterism::Zenoh`: the connection is closed when the router
(or the only peer) goes away, and is not reopened by itself. From then on
`Asterism.poll` returns `false`, `connected?` is false, `lost_reason` says
why, and calls raise `Asterism::Disconnected`. **`Asterism::Zenoh::Error`
never comes out of Asterism; it is always wrapped in `Disconnected`** (the
message is kept). The exposed objects are gone with the connection: to go
on, the application calls `connect` and `expose` again.

## Values

Only what MessagePack carries: `nil`, `true`, `false`, `Integer` (64 bit),
`Float`, `String`, `Array`, `Hash`. A `Symbol` is sent as a `String` (also
as a Hash key: keyword arguments arrive as Symbols again). Anything else in
the arguments raises `EncodeError` before sending; in a return value the
caller gets `RemoteError` (`Asterism::EncodeError`). Nesting is limited to
16 levels.

## Keys and encoding

| Key | Zenoh | Payload |
|---|---|---|
| `asterism/<node>/<app>/<object>/call` | get / queryable | query: `[method, [args...], {kwargs}]`; reply: `["ok", value]` or `["error", class name, message]` |
| `asterism/<node>/<app>/<object>/meta` | get / queryable | reply: `{"methods" => [[name, arity], ...]}` (arity `-1` when not declared). The object may be `*` in the query: one reply per object |
| `asterism/<node>` | liveliness | the node is connected |
| `asterism/<node>/<app>/<object>` | liveliness | the object is exposed |

One queryable per application (`asterism/<node>/<app>/**`). A call whose
node or app is a wildcard reaches every application that exposes that
object, each answering with its own key; `Asterism` itself only sends calls
to one object.

## How waiting works

Everything is polled; nothing runs behind the application. A call that
waits keeps polling (pausing 2 ms between polls) and answers the calls that
come in meanwhile, so two machines calling each other at the same time do
not lock up. An answered call may itself call and wait; such waits nest at
most `Asterism::MAX_NESTING` (4) deep, beyond that the call raises `Error`
(the caller of the outer call gets it as `RemoteError`). While a call
waits, the rest of that application (drawing, input) waits too; other
applications do not.

Calls to an object of the same application (`<node>/<app>` is this one) do
not go out (a Zenoh session does not see its own queryable): they are run
in place, with the same encoding and checks.

## Stack

A waiting call polls Zenoh on the caller's C stack. Measured on the P4
(Family mruby, 16 KB application stack): the application idles at 8.1 KB
used; a waiting call made from the update loop takes it to 10.7 KB, also
with calls answered while waiting (two machines relaying calls to each
other). The same call made from an input handler that C calls into (one more
interpreter entry) reached 12.4 KB; an earlier version of this gem, which
took zenoh replies through blocks and fetched the meta from inside
`respond_to_missing?`, overflowed the stack there. Make waiting calls from
the update loop, or use `async`.

## Limits

- Objects cannot be passed by reference (a return value is a copy), blocks
  cannot be sent, there are no events (later stages).
- The node token `asterism/<node>` is shared by every application of that
  node; when one of them closes, the others still list the node through
  their objects, but a watcher may see the node token go away.
- No authentication: a trusted LAN is assumed.
