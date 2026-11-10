#!/usr/bin/env ruby
# Host tests for the kernel's msgpack (main/prebuild_scripts/spinel/
# msgpack_pure.rb, doc/asterism_file_transfer report/t8d.md): every integer
# width and its boundaries decode to the value when this machine's Integer
# holds it, and are refused with the type and bytes when it does not; what
# the kernel packs reads back.
#
# Three layers:
#   1. CRuby, the Integer width as on a 64-bit machine (8 bytes) and as on a
#      32-bit one (4 bytes, MessagePack.int_bytes set by hand). Each line of
#      cases.rb is checked against a value worked out here with CRuby's own
#      integers. CRuby never overflows, so this checks the decisions (fits or
#      not, the value, the reason), not the arithmetic on the way.
#   2. The same cases compiled by Spinel and run as a 64-bit and a 32-bit
#      (cc -m32) program on the firmware's runtime (components/fmrb_spinel_rt/
#      spinel_rt, built here into test/msgpack/build/). These must print
#      exactly what layer 1 printed for the same width: this is where an
#      intermediate overflowing a 32-bit sp_int (`<<`, a literal that does not
#      exist on 32-bit) would show.
#   3. Skipped, with the reason, when the Spinel compiler or a 32-bit cc is
#      not there (CI): layer 1 still runs.
#
#   ruby test/msgpack/run.rb      (rake msgpack:test, in rake test)

require "stringio"
require "fileutils"
require "open3"

ROOT = File.expand_path("../..", __dir__)
SRC = File.join(ROOT, "main/prebuild_scripts/spinel/msgpack_pure.rb")
CASES = File.join(__dir__, "cases.rb")
BUILD = File.join(__dir__, "build")
RT_DIR = File.join(ROOT, "components/fmrb_spinel_rt/spinel_rt")

$failed = 0
def fail!(msg)
  puts "  FAIL #{msg}"
  $failed += 1
end

load SRC

# ---- layer 1: CRuby -------------------------------------------------------

def run_cruby(ib)
  MessagePack.define_singleton_method(:int_bytes) { ib }
  Object.send(:remove_const, :MP_HEX) if Object.const_defined?(:MP_HEX)
  out = StringIO.new
  saved = $stdout
  $stdout = out
  begin
    load CASES
  ensure
    $stdout = saved
  end
  out.string
end

KINDS = {
  0xcc => ["uint8", 1, false], 0xcd => ["uint16", 2, false], 0xce => ["uint32", 4, false],
  0xcf => ["uint64", 8, false], 0xd0 => ["int8", 1, true], 0xd1 => ["int16", 2, true],
  0xd2 => ["int32", 4, true], 0xd3 => ["int64", 8, true]
}.freeze

# What one integer (hex of its msgpack bytes, prefix first) should come out as
# on a machine whose Integer has ib bytes.
def want_int(hex, ib)
  c = hex[0, 2].to_i(16)
  return c.to_s if c < 0x80
  return (c - 0x100).to_s if c >= 0xe0
  kind, n, signed = KINDS.fetch(c)
  body = hex[2, n * 2]
  v = body.to_i(16)
  v -= 1 << (n * 8) if signed && v >= 1 << (n * 8 - 1)
  lim = (1 << (ib * 8 - 1)) - 1
  return v.to_s if v.between?(-lim, lim)
  "refused: MessagePack: #{kind} 0x#{body} does not fit this machine's #{ib * 8}-bit Integer"
end

MAP_HEAD = "82a3636d64a178a176" # {"cmd" => "x", "v" => ...}

def want_line(hex, ib)
  if hex.start_with?(MAP_HEAD)
    w = want_int(hex[MAP_HEAD.size..], ib)
    return w.start_with?("refused") ? w : "cmd=x v=#{w}"
  end
  want_int(hex, ib)
end

# The bytes msgpack's spec gives for v in its smallest form.
def want_pack(v)
  return format("%02x", v) if v.between?(0, 127)
  return format("%02x", v + 0x100) if v.between?(-32, -1)
  return "cc" + format("%02x", v) if v.between?(0, 0xff)
  return "cd" + format("%04x", v) if v.between?(0, 0xffff)
  return "ce" + format("%08x", v) if v.between?(0, 0xffffffff)
  return "d0" + format("%02x", v & 0xff) if v.between?(-128, 127)
  return "d1" + format("%04x", v & 0xffff) if v.between?(-32768, 32767)
  return "d2" + format("%08x", v & 0xffffffff) if v.between?(-(1 << 31), (1 << 31) - 1)
  "d3" + format("%016x", v & 0xffffffffffffffff)
end

def check_lines(text, ib, label)
  lines = text.lines.map(&:chomp)
  fail!("#{label}: int_bytes line is #{lines.first.inspect}") unless lines.first == "int_bytes #{ib}"
  ncase = 0
  lines.drop(1).each do |l|
    if (m = l.match(/\Apack (-?\d+) => (.*)\z/))
      v = m[1].to_i
      want = "#{want_pack(v)} => #{v}"
      fail!("#{label}: pack #{v}: got #{m[2].inspect}, want #{want.inspect}") unless m[2] == want
    else
      hex, got = l.split(" => ", 2)
      want = want_line(hex, ib)
      fail!("#{label}: #{hex}: got #{got.inspect}, want #{want.inspect}") unless got == want
    end
    ncase += 1
  end
  ncase
end

outs = {}
[8, 4].each do |ib|
  outs[ib] = run_cruby(ib)
  n = check_lines(outs[ib], ib, "CRuby #{ib * 8}-bit")
  puts "  ok   CRuby, #{ib * 8}-bit Integer: #{n} cases" if $failed.zero?
end

# ---- layer 2: Spinel, 64-bit and 32-bit -------------------------------------

def spinel_bin
  dir = ENV["SPINEL_DIR"].to_s.empty? ? File.join(ROOT, "vendor/spinel") : ENV["SPINEL_DIR"]
  bin = File.join(dir, "bin/spinel")
  File.executable?(bin) ? bin : nil
end

def sh(*cmd)
  out, st = Open3.capture2e(*cmd)
  [st.success?, out]
end

CFLAGS = %w[-O1 -w -ffp-contract=off -ffunction-sections -fdata-sections -DSP_INT_OVERFLOW_MODE_RAISE].freeze

# The firmware's runtime snapshot as objects for cc -mBITS, rebuilt when the
# snapshot changes (IMPORT_INFO names its commit).
def runtime_objs(bits)
  info = File.read(File.join(RT_DIR, "IMPORT_INFO"))
  stamp = info[/^fork_commit: (\h+)/, 1].to_s
  srcs = info[/^compile_srcs: (.*)$/, 1].to_s.split
  dir = File.join(BUILD, "rt#{bits}")
  stamp_file = File.join(dir, ".stamp")
  objs = srcs.map { |s| File.join(dir, s.sub(/\.c\z/, ".o")) }
  return objs if File.exist?(stamp_file) && File.read(stamp_file) == stamp && objs.all? { |o| File.exist?(o) }
  FileUtils.rm_rf(dir)
  FileUtils.mkdir_p(dir)
  srcs.zip(objs).each do |s, o|
    ok, out = sh("cc", "-m#{bits}", *CFLAGS, "-I#{RT_DIR}", "-c", File.join(RT_DIR, s), "-o", o)
    raise "runtime #{s} (-m#{bits}): #{out}" unless ok
  end
  File.write(stamp_file, stamp)
  objs
end

def cc_m32?
  FileUtils.mkdir_p(BUILD)
  probe = File.join(BUILD, "m32_probe.c")
  File.write(probe, "int main(void){return sizeof(void*)==4?0:1;}\n")
  exe = File.join(BUILD, "m32_probe")
  ok, = sh("cc", "-m32", probe, "-o", exe)
  ok && system(exe)
end

bin = spinel_bin
if bin.nil?
  puts "  skip Spinel: no compiler (vendor/spinel or SPINEL_DIR); `rake spinel:setup` builds it"
elsif !cc_m32?
  puts "  skip Spinel: cc -m32 does not build here (install gcc-multilib)"
else
  FileUtils.mkdir_p(BUILD)
  rb = File.join(BUILD, "cases_spinel.rb")
  File.write(rb, File.read(SRC) + "\n" + File.read(CASES))
  c = File.join(BUILD, "cases_spinel.c")
  ok, out = sh(bin, "-c", rb, "-o", c)
  fail!("spinel -c: #{out}") unless ok
  if ok
    { 64 => 8, 32 => 4 }.each do |bits, ib|
      exe = File.join(BUILD, "cases_spinel#{bits}")
      ok, out = sh("cc", "-m#{bits}", *CFLAGS, "-I#{RT_DIR}", c, *runtime_objs(bits), "-Wl,--gc-sections", "-lm", "-o", exe)
      unless ok
        fail!("cc -m#{bits}: #{out}")
        next
      end
      got, st = Open3.capture2e(exe)
      fail!("Spinel #{bits}-bit exited #{st.exitstatus}") unless st.success?
      if got == outs[ib]
        puts "  ok   Spinel, #{bits}-bit program: the same #{got.lines.size - 1} lines as CRuby"
      else
        a = outs[ib].lines
        got.lines.each_with_index do |l, i|
          fail!("Spinel #{bits}-bit line #{i + 1}: #{l.chomp.inspect}, CRuby #{a[i].to_s.chomp.inspect}") if l != a[i]
        end
        fail!("Spinel #{bits}-bit: #{got.lines.size} lines, CRuby #{a.size}") if got.lines.size != a.size
      end
    end
  end
end

if $failed.zero?
  puts "msgpack: all ok"
else
  puts "msgpack: #{$failed} failed"
  exit 1
end
