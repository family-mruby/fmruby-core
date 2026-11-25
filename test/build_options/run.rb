#!/usr/bin/env ruby
# Host tests for the ESP32 build's option guard (rakelib/build_options.rb,
# doc/api_fixes F9), on build directories made up in a scratch place:
#
#   - the same options go on; one that differs stops and names it, with the
#     values on both sides and `rake clean_all`
#   - the values are the ones the build uses: unset and "1" are the same
#     FMRB_FONT_JA, the P4 has Asterism whatever FMRB_ASTERISM says, an
#     editorless build has no editor engine
#   - a build/ without the record: no CMake cache goes on; a cache goes on
#     unless its HW target (or F8's FMRB_EDITOR file) says otherwise; the
#     record written then replaces F8's file
#   - the flash wording, and the BUILD_OPTS_OK way out
#   - the Rakefile and the tasks: build:esp32 runs the guard before :setup,
#     every flash task has it, and the Rakefile passes every option the guard
#     compares into the container
#
#   ruby test/build_options/run.rb        (or: rake build_options:test)
require "tmpdir"
require "fileutils"
require_relative "../../rakelib/build_options"

ROOT = File.expand_path("../..", __dir__)
B = FmrbBuildOptions

$failures = 0
$count = 0

def check(name, got, want)
  $count += 1
  return if got == want

  $failures += 1
  puts "FAIL #{name}: got #{got.inspect}, want #{want.inspect}"
end

def includes(name, text, part)
  check(name, text.to_s.include?(part), true)
  puts "     in: #{text.inspect}" unless text.to_s.include?(part)
end

IMAGE = "ghcr.io/family-mruby/fmruby-esp32-build:v5.5.4"
P4 = { "FMRB_HW_TARGET" => "NARYAv4", "FMRB_KERNEL_ENGINE" => "spinel", "FMRB_APP_ENGINE_EDITOR" => "spinel" }.freeze
S3 = { "FMRB_HW_TARGET" => "NARYAv3", "FMRB_ASTERISM" => "1" }.freeze

# A configured build/ (and its sdkconfig) for chip, with the HW target CMake
# keeps (nil: none, as for a build that named no board).
def configured(dir, chip, hw)
  FileUtils.mkdir_p(dir)
  File.write(File.join(dir, "CMakeCache.txt"),
             "IDF_TARGET:STRING=#{chip}\n#{hw ? "FMRB_HW_TARGET:STRING=#{hw}\n" : ''}")
  sdk = File.join(dir, "..", "sdkconfig")
  File.write(sdk, "CONFIG_IDF_TARGET=\"#{chip}\"\n")
  sdk
end

# ---- the values --------------------------------------------------------------

p4 = B.current(P4, IMAGE)
check("unset FONT_JA is 1", p4["FMRB_FONT_JA"], "1")
check("FONT_JA=1 is the same", B.current(P4.merge("FMRB_FONT_JA" => "1"), IMAGE), p4)
check("P4 has Asterism", B.current(P4.merge("FMRB_ASTERISM" => "0"), IMAGE), p4)
check("P4 has the IMU", B.current(P4.merge("FMRB_IMU" => "0"), IMAGE)["FMRB_IMU"], "1")
check("S3 Asterism follows the variable", B.current(S3.merge("FMRB_ASTERISM" => "0"), IMAGE)["FMRB_ASTERISM"], "0")
check("no editor, no editor engine", B.current(P4.merge("FMRB_EDITOR" => "0"), IMAGE)["FMRB_APP_ENGINE_EDITOR"], "none")
check("empty engine is mruby", B.current(P4.merge("FMRB_KERNEL_ENGINE" => ""), IMAGE)["FMRB_KERNEL_ENGINE"], "mruby")
check("ppa and unset are one backend", B.current(P4.merge("FMRB_DISPLAY_BACKEND" => "ppa"), IMAGE), p4)
check("every name has a value", B::NAMES.all? { |k| p4.key?(k) }, true)
check("no value beyond the names", (p4.keys - B::NAMES), [])

Dir.mktmpdir("fmrb_build_options") do |tmp|
  build = File.join(tmp, "a", "build")

  # ---- not configured: nothing to compare ------------------------------------
  FileUtils.mkdir_p(build)
  check("no cache goes on", B.refusal(build, p4), nil)

  # ---- recorded ---------------------------------------------------------------
  sdk = configured(build, "esp32p4", "NARYAv4")
  B.write(build, p4)
  check("record written", File.exist?(File.join(build, B::FILE)), true)
  check("same options go on", B.refusal(build, p4, sdkconfig: sdk), nil)
  noja = B.current(P4.merge("FMRB_FONT_JA" => "0"), IMAGE)
  msg = B.refusal(build, noja, sdkconfig: sdk)
  check("one differs: stops", msg.nil?, false)
  includes("names it with both values", msg, "FMRB_FONT_JA  1 (#{build}/) -> 0 (now)")
  includes("says clean_all", msg, "`rake clean_all`")
  includes("says the sim stays", msg, "build-linux/")
  includes("the options it was made with", msg, "FMRB_FONT_JA=1")
  includes("the way out", msg, "BUILD_OPTS_OK=1")
  check("only the one that differs", msg.lines.count { |l| l.start_with?("  FMRB") }, 1)
  check("allow goes on", B.refusal(build, noja, sdkconfig: sdk, allow: true), nil)
  other = B.current(S3, IMAGE)
  msg = B.refusal(build, other, what: "flash", task: "flash", sdkconfig: sdk)
  includes("flash: names the board", msg, "FMRB_HW_TARGET")
  includes("flash: NARYAv4 -> NARYAv3", msg, "NARYAv4 (#{build}/) -> NARYAv3 (now)")
  includes("flash: says to flash with the build's options", msg, "Flash it with")
  includes("flash: task name", msg, "flash: ")
  img = B.refusal(build, p4.merge("DOCKER_IMAGE" => "x:v6"), sdkconfig: sdk)
  includes("another image stops", img, "DOCKER_IMAGE")
  File.write(File.join(build, B::FILE), "{broken")
  check("a broken record falls back to the cache", B.refusal(build, p4, sdkconfig: sdk), nil)

  # ---- no record (a build/ from before F9) -----------------------------------
  b2 = File.join(tmp, "b", "build")
  sdk2 = configured(b2, "esp32p4", "NARYAv4")
  check("unrecorded, same board: goes on", B.refusal(b2, p4, sdkconfig: sdk2), nil)
  check("unrecorded: other unknowns not compared", B.refusal(b2, noja, sdkconfig: sdk2), nil)
  msg = B.refusal(b2, other, sdkconfig: sdk2)
  includes("unrecorded, other board: stops", msg, "FMRB_HW_TARGET")
  includes("unrecorded: says where it looked", msg, "as its CMake cache tells")
  File.write(File.join(b2, B::LEGACY_EDITOR_FILE), "0\n")
  msg = B.refusal(b2, p4, sdkconfig: sdk2)
  includes("F8's editor file is read", msg, "FMRB_EDITOR  0")
  File.write(File.join(b2, B::LEGACY_EDITOR_FILE), "1\n")
  check("F8's editor file, same value", B.refusal(b2, p4, sdkconfig: sdk2), nil)
  B.write(b2, p4)
  check("the record replaces F8's file", File.exist?(File.join(b2, B::LEGACY_EDITOR_FILE)), false)
  File.delete(File.join(b2, B::FILE))
  msg = B.refusal(b2, B.current(P4.merge("FMRB_EDITOR" => "0"), IMAGE), sdkconfig: sdk2)
  includes("no F8 file: the editor was in", msg, "FMRB_EDITOR  1")

  b3 = File.join(tmp, "c", "build")
  sdk3 = configured(b3, "esp32s3", nil)
  none = B.current({}, IMAGE)
  check("no target in the cache is the unnamed board", B.refusal(b3, none, sdkconfig: sdk3), nil)
  includes("unnamed -> NARYAv3 stops", B.refusal(b3, other, sdkconfig: sdk3), "(none) (#{b3}/) -> NARYAv3")
  b4 = File.join(tmp, "d", "build")
  sdk4 = configured(b4, "esp32s3", "NARYAv4")
  check("cache and chip disagree: target not compared", B.refusal(b4, other, sdkconfig: sdk4), nil)
end

# ---- where the guard sits ------------------------------------------------------

build_rake = File.read(File.join(ROOT, "rakelib/build.rake"))
check("build:esp32 guards before setup",
      build_rake.include?("task :esp32 => [:esp32_options, :setup] do"), true)
check("F8's guard is gone", build_rake.include?("editor_opt"), false)
device = File.read(File.join(ROOT, "rakelib/device.rake"))
check("rake flash guards", device.include?("task :flash => :flash_options do"), true)
%w[app keep_home app_sync keep_home_serial].each do |t|
  check("flash:#{t} guards", device.include?("task :#{t} => :flash_options do"), true)
end
rakefile = File.read(File.join(ROOT, "Rakefile"))
(B::NAMES - %w[FMRB_HW_TARGET DOCKER_IMAGE]).each do |k|
  check("#{k} reaches the container", rakefile.include?("\"-e #{k}="), true)
end

if $failures.zero?
  puts "build options: #{$count} checks ok"
else
  puts "build options: #{$failures} of #{$count} checks FAILED"
  exit 1
end
