# frozen_string_literal: false
#
# Host tests for the ROS 2 message types of Asterism (doc/ruby_asterism, R3):
# the generator (lib/add/picoruby-asterism/tools/asterism_msggen.rb), the
# CDR layer (mrblib/cdr.rb) and the bundled types
# (flash/usr/share/asterism/msgs), all under host Ruby with no docker.
#
# What the fixtures are (refresh: tools/fmrb_ros2_types.rb in the parent repo):
# - ros2_jazzy/type_hashes.txt: the hashes in ROS 2 Jazzy's type description
#   JSON for every bundled type and what it references.
# - test_type_hashes.txt: rosidl's hashes for the test package
#   asterism_test_msgs (bounds, wstring, defaults, a service).
# - golden_cdr.tsv: CDR bytes rclpy made for golden_cases.json. Padding
#   bytes there are whatever was in Fast-CDR's buffer, and the buffer may be
#   longer than the message, so only the bytes this Writer writes as data
#   are compared.
require "json"
require "tmpdir"

CORE = File.expand_path("../..", __dir__)
GEM = File.join(CORE, "lib/add/picoruby-asterism")
TOOLS = File.join(GEM, "tools")
VENDOR = File.join(TOOLS, "ros2_jazzy")
BUNDLE = File.join(CORE, "flash/usr/share/asterism/msgs")
HERE = __dir__

require File.join(TOOLS, "asterism_msggen")

# The pure Ruby layers as they ship. Asterism.now_ms / pause are only used
# by calls that wait, which these tests do not make.
module Asterism; end
load File.join(GEM, "mrblib/cdr.rb")
load File.join(GEM, "mrblib/ros.rb")

$fails = 0
$checks = 0

def check(cond, what)
  $checks += 1
  return if cond
  $fails += 1
  puts "FAIL: #{what}"
end

def raises(klass, what)
  yield
  check(false, "#{what}: no #{klass}")
rescue klass
  check(true, what)
rescue StandardError => e
  check(false, "#{what}: #{e.class}: #{e.message} instead of #{klass}")
end

def hex(s)
  s.unpack1("H*")
end

def unhex(h)
  [h].pack("H*")
end

def read_hashes(path)
  File.readlines(path).reject { |l| l.start_with?("#") || l.strip.empty? }.to_h { |l| l.split }
end

bundled = File.readlines(File.join(TOOLS, "bundled_types.txt")).map(&:strip)
              .reject { |l| l.empty? || l.start_with?("#") }

# ---- 1. type hashes -----------------------------------------------------

reg = AsterismMsgGen::Registry.new([VENDOR])
hasher = AsterismMsgGen::Hasher.new(reg)
jazzy = read_hashes(File.join(VENDOR, "type_hashes.txt"))
all = AsterismMsgGen.closure(reg, bundled)
names = all.flat_map { |n| AsterismMsgGen.hash_names(hasher, n) }.uniq
names.each do |n|
  check(jazzy.key?(n), "#{n}: no Jazzy hash recorded")
  check(hasher.type_hash(n) == jazzy[n], "#{n}: computed #{hasher.type_hash(n)}, Jazzy #{jazzy[n]}") if jazzy.key?(n)
end
puts "type hashes: #{names.size} types (#{all.size} bundled with what they use) against Jazzy's JSON"

# The constants R1 and R2 used before the generator.
check(hasher.type_hash("std_msgs/msg/String") ==
      "RIHS01_df668c740482bbd48fb39d76a70dfd4bd59db1288021743503259e948f6b1a18", "std_msgs/String = R1 constant")
check(hasher.type_hash("example_interfaces/srv/AddTwoInts") ==
      "RIHS01_e118de6bf5eeb66a2491b5bda11202e7b68f198d6f67922cf30364858239c81a", "AddTwoInts = R2 constant")

treg = AsterismMsgGen::Registry.new([VENDOR, HERE])
thasher = AsterismMsgGen::Hasher.new(treg)
ref = read_hashes(File.join(HERE, "test_type_hashes.txt"))
ref.each { |n, h| check(thasher.type_hash(n) == h, "#{n}: computed #{thasher.type_hash(n)}, rosidl #{h}") }
puts "type hashes: #{ref.size} types of asterism_test_msgs against rosidl"

# ---- 2. the bundle is what the generator makes ---------------------------

em = AsterismMsgGen::Emitter.new(reg, hasher)
expected = all.map { |n| AsterismMsgGen::Emitter.rel_path(n) }.sort
present = Dir.glob("**/*.rb", base: BUNDLE).sort
check(present == expected, "bundle files differ: extra #{present - expected}, missing #{expected - present}")
all.each do |n|
  path = File.join(BUNDLE, AsterismMsgGen::Emitter.rel_path(n))
  next unless File.file?(path)
  check(File.read(path) == em.file(n), "#{path} is not what the generator makes (regenerate)")
end
puts "bundle: #{present.size} files match the generator"

# ---- 3. loading -----------------------------------------------------------

Dir.mktmpdir("asterism_msgs") do |tmp|
  # The test package goes to a directory of its own, as a user's would.
  tem = AsterismMsgGen::Emitter.new(treg, thasher)
  TEST_TYPES = %w[asterism_test_msgs/msg/Bounds asterism_test_msgs/msg/Align asterism_test_msgs/msg/Defaults
                  asterism_test_msgs/msg/Wide asterism_test_msgs/srv/Lookup].freeze
  AsterismMsgGen.closure(treg, TEST_TYPES).each do |n|
    next unless n.start_with?("asterism_test_msgs")
    path = File.join(tmp, AsterismMsgGen::Emitter.rel_path(n))
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, tem.file(n))
  end
  Asterism::ROS::TYPE_PATH.replace([BUNDLE, tmp])

  types = {}
  bundled.each { |n| types[n] = Asterism::ROS.require_type(n) }
  TEST_TYPES.each { |n| Asterism::ROS.require_type(n) }
  check(Asterism::ROS::GeometryMsgs::Vector3.is_a?(Class), "Twist loaded Vector3 with it")
  check(Asterism::ROS.require_type("geometry_msgs/msg/Twist").equal?(types["geometry_msgs/msg/Twist"]), "second require_type")
  raises(Asterism::ROS::UnknownType, "unknown type") { Asterism::ROS.require_type("nav_msgs/msg/Odometry") }
  raises(ArgumentError, "bad name") { Asterism::ROS.require_type("Twist") }
  check(Asterism::ROS.type_of("std_msgs/msg/String") == Asterism::ROS::StdMsgs::String, "type_of by name")
  puts "loading: #{types.size} bundled types with require_type"

  # ---- 4. values ----------------------------------------------------------

  tw = Asterism::ROS::GeometryMsgs::Twist
  t = tw.from(linear: { x: 0.1 })
  check(t.linear.x == 0.1 && t.linear.y == 0.0 && t.angular.z == 0.0, "Hash with missing fields")
  check(tw.from("linear" => { "x" => 2.0 }).linear.x == 2.0, "String keys")
  check(t.to_h == { linear: { x: 0.1, y: 0.0, z: 0.0 }, angular: { x: 0.0, y: 0.0, z: 0.0 } }, "to_h")
  check(tw.from(t).equal?(t), "from(itself)")
  check(tw.new == tw.from(nil), "== and defaults")
  raises(ArgumentError, "unknown field") { tw.from(linear: { w: 1 }) }
  raises(TypeError, "not a Hash") { tw.from(3) }
  q = Asterism::ROS::GeometryMsgs::Quaternion.new
  check(q.w == 1.0, "default from the .msg (Quaternion w 1)")
  check(Asterism::ROS::SensorMsgs::BatteryState::POWER_SUPPLY_STATUS_FULL == 4, "constant")
  check(Asterism::ROS::StdMsgs::Empty.decode(Asterism::ROS::StdMsgs::Empty.encode({})) == Asterism::ROS::StdMsgs::Empty.new, "Empty")
  imu = Asterism::ROS::SensorMsgs::Imu.new
  check(imu.orientation_covariance == [0.0] * 9, "fixed array default")
  check(imu.inspect.start_with?("#<sensor_msgs/msg/Imu {"), "inspect")
  d = Asterism::ROS::AsterismTestMsgs::Defaults.new
  check(d.a == 7 && d.s == "hi # not a comment" && d.f == [1.0, 2.0, 3.0] && d.b == true && d.g == [-1, 2] &&
        d.raw == "\x01\x02".b && d.words == %w[x y], "defaults of every kind")
  check(Asterism::ROS::AsterismTestMsgs::Defaults::K == 3 && Asterism::ROS::AsterismTestMsgs::Defaults::NAME == "hello world" &&
        Asterism::ROS::AsterismTestMsgs::Defaults::PI == 3.14, "constants of every kind")
  puts "values: from / to_h / defaults / constants"

  # ---- 5. CDR against ROS 2 (golden bytes) ---------------------------------

  # A Writer that remembers which bytes are padding.
  module PadTrack
    def pads
      @pads ||= []
    end

    def align(n)
      before = @buf.bytesize
      super
      (before...@buf.bytesize).each { |i| pads << i }
      self
    end
  end

  def norm(v)
    case v
    when Hash then v.to_h { |k, x| [k.to_sym, norm(x)] }
    when Array then v.map { |x| norm(x) }
    when String then v.encoding == Encoding::BINARY ? v.bytes : v
    when Float then v
    when Integer then v.to_f
    else v
    end
  end

  # a and b equal, floats within float32 precision.
  def close?(a, b)
    case a
    when Hash then b.is_a?(Hash) && a.keys == b.keys && a.keys.all? { |k| close?(a[k], b[k]) }
    when Array then b.is_a?(Array) && a.size == b.size && a.each_index.all? { |i| close?(a[i], b[i]) }
    when Float then b.is_a?(Float) && (a == b || (a - b).abs <= 1e-6 * [a.abs, b.abs].max)
    else a == b
    end
  end

  def type_for(name)
    if name.include?("/srv/")
      base, part = name.split(/_(?=Request\z|Response\z)/)
      Asterism::ROS.require_type(base).const_get(part)
    else
      Asterism::ROS.require_type(name)
    end
  end

  golden = File.readlines(File.join(HERE, "golden_cdr.tsv")).map { |l| l.chomp.split("\t") }
  golden.each do |name, json, h|
    t = type_for(name)
    values = JSON.parse(json)
    ros = unhex(h)
    w = Asterism::CDR::Writer.new
    w.extend(PadTrack)
    t.write(w, t.from(values))
    mine = w.to_s
    same = mine.bytesize <= ros.bytesize &&
           (0...mine.bytesize).all? { |i| w.pads.include?(i) || mine.getbyte(i) == ros.getbyte(i) }
    check(same, "#{name} #{json}: encode\n  mine #{hex(mine)}\n  ros  #{h}")
    got = t.decode(ros)
    check(close?(norm(got.to_h), norm(t.from(values).to_h)), "#{name} #{json}: decode #{got.inspect}")
    check(t.decode(t.encode(got)) == got, "#{name}: decode(encode(decode(ros)))")
  end
  puts "CDR: #{golden.size} messages against rclpy's bytes (rmw_zenoh_cpp, Fast-CDR)"

  # ---- 6. round trips of every bundled type ---------------------------------

  rnd = Random.new(42)
  filler = lambda do |msg_def|
    h = {}
    msg_def.fields.each do |f|
      one = lambda do
        if f.nested
          filler.call(reg.message(f.nested))
        else
          case f.base
          when "bool" then rnd.rand(2) == 1
          when "string" then "s#{rnd.rand(1000)}"[0, f.string_max || 10]
          when "float32" then rnd.rand(-1000..1000) / 8.0
          when "float64" then rnd.rand * 1e6 - 5e5
          when "int8" then rnd.rand(-128..127)
          when "uint8", "byte", "char" then rnd.rand(0..255)
          when "int16" then rnd.rand(-32_768..32_767)
          when "uint16" then rnd.rand(0..65_535)
          when "int32" then rnd.rand(-2**31..2**31 - 1)
          when "uint32" then rnd.rand(0..2**32 - 1)
          when "int64" then rnd.rand(-2**63..2**63 - 1)
          when "uint64" then rnd.rand(0..2**63 - 1)
          end
        end
      end
      h[f.name.to_sym] = case f.array
                         when nil then one.call
                         when :fixed then Array.new(f.size) { one.call }
                         else Array.new(rnd.rand(0..(f.size || 4))) { one.call }
                         end
    end
    h
  end
  n = 0
  all.each do |full|
    tdef = reg.type(full)
    msgs = tdef.is_a?(AsterismMsgGen::Service) ? [tdef.request, tdef.response] : [tdef]
    msgs.each do |m|
      t = tdef.is_a?(AsterismMsgGen::Service) ? type_for(m.full_name) : Asterism::ROS.require_type(full)
      3.times do
        v = t.from(filler.call(m))
        back = t.decode(t.encode(v))
        check(close?(norm(back.to_h), norm(v.to_h)), "#{m.full_name}: round trip #{v.inspect} -> #{back.inspect}")
        n += 1
      end
      check(t.decode(t.encode(nil)) == t.new, "#{m.full_name}: defaults round trip")
    end
  end
  puts "round trips: #{n} random messages of #{all.size} types"

  # ---- 7. bounds, empty sequences, alignment -----------------------------

  b = Asterism::ROS::AsterismTestMsgs::Bounds
  full = { short_name: "abcde", codes: %w[abc xy], few: [1, 2, 3], pair: [1.0, 2.0], small_bytes: [1, 2, 3, 4],
           points: [{ x: 1.0 }, { y: 2.0 }], tags: %w[a b], flags: [true, false, true], short_tags: %w[abcd ef] }
  bb = b.decode(b.encode(full))
  check(bb.short_name == "abcde" && bb.codes == %w[abc xy] && bb.few == [1, 2, 3] && bb.small_bytes == "\x01\x02\x03\x04".b &&
        bb.points[1].y == 2.0 && bb.flags == [true, false, true] && bb.short_tags == %w[abcd ef], "every bound at its maximum")
  raises(ArgumentError, "string over its bound") { b.encode(short_name: "abcdef") }
  raises(ArgumentError, "string in an array over its bound") { b.encode(codes: %w[abcd x]) }
  raises(ArgumentError, "bounded sequence over its bound") { b.encode(few: [1, 2, 3, 4]) }
  raises(ArgumentError, "bounded byte sequence over its bound") { b.encode(small_bytes: "12345") }
  raises(ArgumentError, "bounded struct sequence over its bound") { b.encode(points: [{}, {}, {}]) }
  raises(ArgumentError, "fixed array of the wrong length") { b.encode(pair: [1.0]) }
  raises(ArgumentError, "bounded string in a bounded sequence") { b.encode(short_tags: %w[abcde]) }
  e = b.decode(b.encode({}))
  check(e.few == [] && e.points == [] && e.small_bytes == "".b && e.pair == [0.0, 0.0] && e.codes == ["", ""], "empty sequences")
  # Empty sequence of float64 then an int32: no padding for the missing elements.
  check(hex(Asterism::ROS::SensorMsgs::JointState.encode(position: [], velocity: [1.0])) ==
        "00010000" "00000000" "00000000" "0100000000" "000000" "00000000" "00000000" "01000000" "00000000" \
        "000000000000f03f" "00000000", "empty float64 sequence (no alignment for no elements)")

  al = Asterism::ROS::AsterismTestMsgs::Align
  bytes = al.encode(a: 1, b: 2.0, c: 3, v: { x: 4.0 }, d: 5, h: { frame_id: "abc" }, e: [6], ch: 7, by: 8, three: [9, 10, 11],
                    nested: { short_name: "q" }, f: 1.5, many: [{ few: [1] }], last: -1)
  r = Asterism::CDR::Reader.new(bytes)
  check(r.uint8 == 1, "align: uint8 at 0")
  check(r.float64 == 2.0 && r.pos == 4 + 16, "align: float64 after a uint8 is at 8")
  check(r.uint8 == 3, "align: uint8")
  check(r.float64 == 4.0 && r.pos == 4 + 32, "align: a nested struct aligns on its first member")
  back = al.decode(bytes)
  check(back.many[0].few == [1] && back.last == -1 && back.nested.short_name == "q" && back.f == 1.5 && back.three == "\x09\x0a\x0b".b,
        "align: everything comes back")

  raises(NotImplementedError, "wstring") { Asterism::ROS::AsterismTestMsgs::Wide.encode({}) }
  raises(Asterism::CDR::DecodeError, "truncated") { Asterism::ROS::GeometryMsgs::Twist.decode("\x00\x01\x00\x00\x00") }
  raises(Asterism::CDR::DecodeError, "sequence longer than the data") do
    Asterism::ROS::StdMsgs::Int32MultiArray.decode("\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\x7f")
  end
  # Big-endian input decodes too.
  be = "\x00\x00\x00\x00" + [1.5].pack("G") + [-2.0].pack("G") + [0.25].pack("G")
  v3 = Asterism::ROS::GeometryMsgs::Vector3.decode(be)
  check(v3.x == 1.5 && v3.y == -2.0 && v3.z == 0.25, "big-endian floats")

  lk = Asterism::ROS::AsterismTestMsgs::Lookup
  check(lk::TYPE_NAME == "asterism_test_msgs::srv::dds_::Lookup_" && lk::Request.new.limit == 3, "a user service")
  res = lk::Response.decode(lk::Response.encode(found: [{ short_name: "a" }], ok: true))
  check(res.found[0].short_name == "a" && res.ok == true, "a user service's response")
  puts "bounds / alignment / errors"
end

# ---- 8. floats without Array#pack ----------------------------------------

cdr = Asterism::CDR
float_check = lambda do
  vals = [0.0, -0.0, 1.0, -1.0, 0.1, -0.1, 1.5, 3.4028234663852886e38, 3.5e38, 1.0e-45, 1.4e-45, 1.17549435e-38,
          5.0e-324, 2.2250738585072014e-308, 2.225073858507201e-308, 1.7976931348623157e308, Float::INFINITY,
          -Float::INFINITY, 123_456.789, 1.0000000596046448, 1.0000001192092896, 16_777_217.0]
  rnd = Random.new(7)
  200.times { vals << (rnd.rand - 0.5) * 10.0**rnd.rand(-40..40) }
  vals.each do |v|
    check(cdr.f64_bytes(v) == [v].pack("E"), "f64_bytes(#{v})")
    check(cdr.f32_bytes(v) == [v].pack("e"), "f32_bytes(#{v}): #{hex(cdr.f32_bytes(v))} vs #{hex([v].pack('e'))}")
    b8 = [v].pack("E")
    lo = b8.unpack1("V")
    hi = b8.byteslice(4, 4).unpack1("V")
    back = cdr.f64_from(lo, hi)
    check(back == v && (1.0 / back).positive? == (1.0 / v).positive?, "f64_from(#{v})")
    f = [v].pack("e").unpack1("e")
    back32 = cdr.f32_from([v].pack("e").unpack1("V"))
    check(back32 == f || (back32.nan? && f.nan?), "f32_from(#{v})")
  end
  check(cdr.f64_bytes(Float::NAN).byteslice(6, 2) == "\xf8\x7f".b, "f64 NaN")
  check(cdr.f64_from(0, 0x7ff80000).nan?, "f64_from NaN")
end
float_check.call
# Again without Math.ldexp / Math.frexp (a VM without mruby-math).
[:MATH_LDEXP, :MATH_FREXP].each do |c|
  cdr.send(:remove_const, c)
  cdr.const_set(c, false)
end
float_check.call
puts "floats without pack: 222 values, with and without Math"

puts "#{$checks} checks, #{$fails} failed"
exit($fails.zero? ? 0 : 1)
