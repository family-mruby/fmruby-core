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

## ROS 2 (rmw_zenoh): `Asterism::ROS` and `Asterism::CDR`

A minimal ROS 2 node that talks to ROS 2 systems using rmw_zenoh (checked
with ROS 2 Jazzy, rmw_zenoh_cpp 0.2.11, Zenoh 1.8.0), directly on an
`Asterism::Zenoh::Session`. Pure Ruby, independent of the object layer above
(no `Asterism.connect`, no MessagePack; only its clock and pause helpers).
Topics and services; the types are `std_msgs/String` and
`example_interfaces/srv/AddTwoInts`.

```ruby
s = Asterism::Zenoh::Session.open("tcp/192.168.10.2:7447")
node = Asterism::ROS::Node.new(s, "fmruby_talker")       # namespace: "/", domain: 0
pub = node.publisher("/chatter", Asterism::ROS::StdMsgs::String)
sub = node.subscription("/chatter_back", Asterism::ROS::StdMsgs::String)
loop do
  s.poll
  pub.publish("hello")
  sub.each_pending { |msg, info| puts "#{info && info.sequence}: #{msg}" }
end
node.close                                               # or let the session close
```

Services:

```ruby
add = Asterism::ROS::ExampleInterfaces::AddTwoInts
node.service("/fmruby/add_two_ints", add) { |req| { sum: req.a + req.b } }
cli = node.client("/add_two_ints", add)
loop do
  node.poll                       # session.poll, then answers the requests
  res = cli.call(a: 2, b: 3)      # waits (polling), raises Asterism::ROS::Timeout
  puts res.sum
  c = cli.call_async(a: 2, b: 3)  # returns at once
  # ... later updates:
  puts c.value.sum if c.done?
end
```

| Call | Returns | Notes |
|---|---|---|
| `Asterism::ROS::Node.new(session, name, namespace: "/", domain: 0, enclave: "/")` | `Node` | Declares the node's liveliness token (`ros2 node list`). `name` has no `/`. |
| `node.publisher(topic, type, qos: DEFAULT_QOS)` | `Publisher` | `topic` absolute, or relative to the namespace. Declares the publisher token (`ros2 topic list`). |
| `pub.publish(msg)` | nil | Puts the CDR payload with rmw_zenoh's attachment (sequence number, time, GID). |
| `node.subscription(topic, type, qos: DEFAULT_QOS, depth: 16)` | `Subscription` | Subscribes and declares the subscription token. |
| `sub.each_pending { \|msg, info\| }` | count | `info` is an `Attachment` (`sequence`, `stamp_ns`, `gid`) or nil. Samples that are not valid CDR are skipped and counted in `sub.errors`. |
| `node.service(name, type, qos: DEFAULT_QOS, depth: 8) { \|req\| response }` | `Service` | Serves `name` (`ros2 service list`). The block gets a `type::Request` and returns a `type::Response` or a Hash of its fields. It runs from `node.poll` (or `service.handle_pending`), never behind the application. A request without rmw_zenoh's attachment, or that does not decode, is counted in `service.errors` and gets no answer. An exception from the block ends that request without an answer and is raised from `node.poll`. `service.handled`: answered so far. |
| `node.poll(steps = 8)` | true / false | `session.poll(steps)`, then answers the requests waiting for this node's services. Returns what `session.poll` returns. |
| `node.client(name, type, qos: DEFAULT_QOS)` | `Client` | Declares the client token. |
| `client.call(request = nil, timeout_ms: 2000, **fields)` | `type::Response` | Sends the request (a `type::Request`, a Hash, or the fields as keywords) and waits, polling the node (its services keep answering). `Asterism::ROS::Timeout` when no response came in time, at once when nobody serves the name; `Asterism::Zenoh::Error` when the session closed. |
| `client.call_async(request = nil, timeout_ms: 2000, **fields)` | `Call` | Sends and returns at once. `call.done?` never waits (`node.poll` moves it on); `call.value` waits and returns the response or raises like `call`; `call.response` (nil until it came), `call.took_ms`, `call.sequence`. |
| `node.call(name, type, request = nil, timeout_ms: 2000, **fields)` | `type::Response` | `client.call` through a client made on first use and kept per name. |
| `node.close` / `pub.close` / `sub.close` / `service.close` / `client.close` | nil | Withdraws the tokens (they also go when the session closes). `node.close` closes everything the node made. |
| `Asterism::ROS::ExampleInterfaces::AddTwoInts` | | `Request` (`a`, `b`, int64) and `Response` (`sum`, int64), each with `new(**fields)`, `encode(msg_or_hash)`, `decode(bytes)`, `to_h`. |
| `Asterism::CDR::Writer` / `Reader` | | Plain CDR with the 4-byte header: `uint8 bool uint16 uint32 uint64 int16 int32 int64 string`, aligned from the end of the header. Writes little endian; reads either order. |

What goes on the wire (rmw_zenoh_cpp 0.2.x):

| Item | Form |
|---|---|
| Data key | `<domain>/<topic without the outer "/">/<DDS type>/<type hash>`, e.g. `0/chatter/std_msgs::msg::dds_::String_/RIHS01_df668c74...` |
| Payload | CDR: `00 01 00 00`, uint32 length with the NUL, bytes, NUL |
| Attachment | int64 sequence, int64 time (ns since the epoch), both little endian, one byte GID length (16), 16 bytes GID: 33 bytes. **Required**: rmw_zenoh drops a sample without it |
| Node token | `@ros2_lv/<domain>/<zid>/<nid>/<nid>/NN/<enclave>/<namespace>/<node>` |
| Topic token | `@ros2_lv/<domain>/<zid>/<nid>/<id>/MP` (or `MS`) `/<enclave>/<namespace>/<node>/<topic>/<DDS type>/<type hash>/<qos>` |
| Service key | `<domain>/<service without the outer "/">/<DDS service type>/<service type hash>`, e.g. `0/add_two_ints/example_interfaces::srv::dds_::AddTwoInts_/RIHS01_e118de6b...`. The type and hash are those of the service, not of its Request / Response |
| Service request | a get on the service key, target ALL_COMPLETE, no consolidation, payload the request's CDR, attachment as above (the client's sequence number from 1, time, the client's GID) |
| Service reply | from a **complete** queryable (a non-complete one never sees ALL_COMPLETE queries), on the service key, payload the response's CDR, attachment: the request's sequence number and GID with the server's time. The client pairs reply and request by the sequence number |
| Service tokens | like topic tokens, with `SS` (server) and `SC` (client) |

In tokens, `/` inside a name is written `%` (`/chatter` is `%chatter`, the
root namespace `%`). `<zid>` is the Zenoh session ID (`session.zid`).
`DEFAULT_QOS` (`::,10:,:,:,,`) is rmw_zenoh's form of the default profile:
reliable, volatile, keep last 10.

Not here (later stages): other message types and their type hashes (RIHS01
is computed from the type description; the two here are constants), actions,
QoS other than the default (transient local needs Zenoh's advanced
publisher), name remapping.

### Adding a type

A message type is a module (or class) with `TYPE_NAME`, `TYPE_HASH`,
`encode(msg)` and `decode(bytes)` built on `Asterism::CDR`; a service type
has `TYPE_NAME` / `TYPE_HASH` of the service and `Request` / `Response`
classes with `encode` / `decode` (see `ExampleInterfaces::AddTwoInts`). The
DDS name is `<package>::msg::dds_::<Type>_` (or `::srv::`); the hash can be
read from the ROS 2 installation
(`share/<package>/<msg|srv>/<Type>.json`, the first `RIHS01_` in it) or from
a liveliness token.
