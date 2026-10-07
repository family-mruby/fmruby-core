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
Topics and services. The message and service types are generated from
`.msg` / `.srv` files (see [Message types](#message-types)) and loaded when
the application asks for them.

```ruby
s = Asterism::Zenoh::Session.open("tcp/192.168.10.2:7447")
node = Asterism::ROS::Node.new(s, "fmruby_talker")       # namespace: "/", domain: 0
str = Asterism::ROS.require_type("std_msgs/msg/String")
pub = node.publisher("/chatter", str)
sub = node.subscription("/chatter_back", "std_msgs/msg/String")  # by name works too
loop do
  s.poll
  pub << { data: "hello" }                               # or pub.publish(str.new(data: "hello"))
  sub.each_pending { |msg, info| puts "#{info && info.sequence}: #{msg.data}" }
end
node.close                                               # or let the session close
```

Services:

```ruby
add = Asterism::ROS.require_type("example_interfaces/srv/AddTwoInts")
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
| `node.publisher(topic, type, qos: DEFAULT_QOS)` | `Publisher` | `topic` absolute, or relative to the namespace. `type` is a generated type or its ROS name (`"geometry_msgs/msg/Twist"`, loaded with `require_type`); the same for subscriptions, services and clients. Declares the publisher token (`ros2 topic list`). |
| `pub.publish(msg)` / `pub << msg` | nil / `pub` | `msg` is a message of the type or a Hash of its fields. Puts the CDR payload with rmw_zenoh's attachment (sequence number, time, GID). |
| `node.subscription(topic, type, qos: DEFAULT_QOS, depth: 16)` | `Subscription` | Subscribes and declares the subscription token. |
| `sub.each_pending { \|msg, info\| }` | count | `info` is an `Attachment` (`sequence`, `stamp_ns`, `gid`) or nil. Samples that are not valid CDR are skipped and counted in `sub.errors`. |
| `node.service(name, type, qos: DEFAULT_QOS, depth: 8) { \|req\| response }` | `Service` | Serves `name` (`ros2 service list`). The block gets a `type::Request` and returns a `type::Response` or a Hash of its fields. It runs from `node.poll` (or `service.handle_pending`), never behind the application. A request without rmw_zenoh's attachment, or that does not decode, is counted in `service.errors` and gets no answer. An exception from the block ends that request without an answer and is raised from `node.poll`. `service.handled`: answered so far. |
| `node.poll(steps = 8)` | true / false | `session.poll(steps)`, then answers the requests waiting for this node's services. Returns what `session.poll` returns. |
| `node.client(name, type, qos: DEFAULT_QOS)` | `Client` | Declares the client token. |
| `client.call(request = nil, timeout_ms: 2000, **fields)` | `type::Response` | Sends the request (a `type::Request`, a Hash, or the fields as keywords) and waits, polling the node (its services keep answering). `Asterism::ROS::Timeout` when no response came in time, at once when nobody serves the name; `Asterism::Zenoh::Error` when the session closed. |
| `client.call_async(request = nil, timeout_ms: 2000, **fields)` | `Call` | Sends and returns at once. `call.done?` never waits (`node.poll` moves it on); `call.value` waits and returns the response or raises like `call`; `call.response` (nil until it came), `call.took_ms`, `call.sequence`. |
| `node.call(name, type, request = nil, timeout_ms: 2000, **fields)` | `type::Response` | `client.call` through a client made on first use and kept per name. |
| `node.close` / `pub.close` / `sub.close` / `service.close` / `client.close` | nil | Withdraws the tokens (they also go when the session closes). `node.close` closes everything the node made. |
| `Asterism::ROS.require_type(name)` | the type | Loads `<pkg>/<msg\|srv>/<Name>.rb` from `Asterism::ROS::TYPE_PATH` (`["/usr/share/asterism/msgs"]`; add directories to it) and returns `Asterism::ROS::<Pkg>::<Name>`. A type already loaded (or defined in Ruby) is returned at once. `UnknownType` when there is no file. |
| `Asterism::CDR::Writer` / `Reader` | | Plain CDR with the 4-byte header, aligned from the end of the header: `bool int8 uint8 int16 uint16 int32 uint32 int64 uint64 float32 float64 string`, `array(kind, v, fixed, max)`, `bytes`, `structs(type, ...)`. Writes little endian; reads either order. |

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

Not here (later stages): actions, QoS other than the default (transient
local needs Zenoh's advanced publisher), name remapping, receiving a type
the application has not loaded.

## Message types

### Values

A generated message is a plain Ruby class (`Asterism::ROS::Message` is its
base; the app VM has neither `Data` nor `Struct`). The same code runs on
CRuby.

```ruby
Twist = Asterism::ROS.require_type("geometry_msgs/msg/Twist")
t = Twist.new(linear: { x: 0.1 })     # missing fields take their defaults
t = Twist.from({ "linear" => { "x" => 0.1 } })   # Hash, String keys too
t.linear.x                            # => 0.1
t.angular                             # => #<geometry_msgs/msg/Vector3 {x: 0.0, ...}>
t.to_h                                # => {linear: {x: 0.1, y: 0.0, z: 0.0}, angular: {...}}
bytes = Twist.encode(t)               # or Twist.encode(linear: { x: 0.1 })
Twist.decode(bytes) == t              # => true
Twist::TYPE_NAME                      # "geometry_msgs::msg::dds_::Twist_"
Twist::TYPE_HASH                      # "RIHS01_9c45bf16..."
Asterism::ROS::SensorMsgs::BatteryState::POWER_SUPPLY_STATUS_FULL   # constants
```

| Field in the .msg | Ruby value | Default |
|---|---|---|
| `bool` | true / false | false |
| integers, `byte`, `char` | Integer (a `uint64` at or above 2**63 reads back negative) | 0 |
| `float32` / `float64` | Float | 0.0 |
| `string`, `string<=N` | String (UTF-8 bytes; longer than N raises `ArgumentError` on encode) | "" |
| a message | that type (a Hash is turned into it) | its defaults |
| `T[N]`, `T[]`, `T[<=N]` | Array (a wrong length or over the bound raises `ArgumentError` on encode) | N defaults, or [] |
| `byte[]`, `uint8[]`, `char[]` (any size) | a binary String (an Array of Integers is taken too) | "\0" * N, or "" |
| `wstring` | not supported: encode / decode raise `NotImplementedError` | |

Defaults written in the .msg (`float64 w 1`) are used. Unknown field names
raise `ArgumentError`; integers are not range-checked (they wrap).

### The bundled types

Made from ROS 2 Jazzy's definitions, in `flash/usr/share/asterism/msgs`
(device: `/usr/share/asterism/msgs`), one file per type:
std_msgs (Bool, Byte, Char, String, Empty, the integer and float types,
Header, ColorRGBA, MultiArrayDimension / Layout and every *MultiArray),
builtin_interfaces (Time, Duration), geometry_msgs (Vector3, Point, Point32,
Quaternion, Pose, Pose2D, Twist, Accel, Transform, Wrench and their
*Stamped), sensor_msgs (Imu, BatteryState, Temperature, Range,
MagneticField, Illuminance, FluidPressure, RelativeHumidity, JointState,
NavSatStatus, NavSatFix), example_interfaces/srv/AddTwoInts. The list is
`tools/bundled_types.txt`. Nothing of them is in the firmware: an
application loads what it uses, and each file loads the types it is made of.

### Making types: `tools/asterism_msggen.rb`

CRuby, standard library only, no ROS 2 installation needed (only the
`.msg` / `.srv` files of the packages involved):

```
# a type of an installed ROS 2 (and the types it uses)
ruby tools/asterism_msggen.rb -I /opt/ros/jazzy/share -o out geometry_msgs/msg/Twist
# your own package (my_pkg/msg/Foo.msg; -I for the packages it refers to)
ruby tools/asterism_msggen.rb -I /opt/ros/jazzy/share -o out path/to/my_pkg/msg/Foo.msg
# print the type hashes
ruby tools/asterism_msggen.rb -I share --hash example_interfaces/srv/AddTwoInts
# compare the hashes with the type description JSON ROS 2 installs
ruby tools/asterism_msggen.rb -I /opt/ros/jazzy/share --check-json /opt/ros/jazzy/share sensor_msgs/msg/Imu
```

`-I` directories are laid out like a ROS 2 `share`: `<pkg>/msg/*.msg`,
`<pkg>/srv/*.srv`. The output is `<out>/<pkg>/<msg|srv>/<Name>.rb`; copy it
to the device (`/usr/share/asterism/msgs` or a directory added to
`Asterism::ROS::TYPE_PATH`), or put the output directory in `TYPE_PATH` on
CRuby. A service's type hash needs `service_msgs/msg/ServiceEventInfo` and
`builtin_interfaces/msg/Time` under `-I` (every ROS 2 `share` has them).

The type hash (RIHS01) is computed the way ROS 2 Jazzy does
(`rosidl_generator_type_description`): the type and every type it refers
to, each as `{type_name, fields: [{name, type: {type_id, capacity,
string_capacity, nested_type_name}}]}` without default values, the referred
ones sorted by name, written as Python's `json.dumps(..., separators=(", ",
": "))`, then SHA-256. A service's description is `request_message`,
`response_message` and `event_message` (`<Name>_Event`: `info`, and
`request` / `response` as sequences of at most one). Other ROS 2 versions
may change the rules; regenerate and check with `--check-json` against that
version.

`ros2_jazzy/` next to the generator holds the Jazzy definitions of the
bundled types and the hashes from Jazzy's JSON (`type_hashes.txt`) that the
host tests (`rake asterism:test`) compare with. Both are refreshed from the
ROS 2 image by `tools/fmrb_ros2_types.rb` in the Family mruby repository.
The definitions are from ros2/common_interfaces, ros2/rcl_interfaces and
ros2/example_interfaces (Apache License 2.0).

### Loading

On mruby, `require_type` reads the file and evaluates it in the
application's VM (`Kernel#eval`); PicoRuby's `require` would run each file
in a Sandbox task that stays for the life of the VM. On CRuby it uses
`require`. Measured on the P4 (Family mruby, a 1 MB VM pool):
geometry_msgs/Twist with Vector3 15.3 KB, sensor_msgs/Imu with Header,
Time and Quaternion 32.8 KB more, std_msgs/Float32MultiArray with its
layout types 20.0 KB more (68 KB for nine files, 340 ms). Evaluating a file
compiles it on the caller's stack (about 2.5 KB deeper than the update
loop); load the types in `on_create`, not in a deep call chain.
