#!/usr/bin/env ruby
# frozen_string_literal: true

# Tests of the sig coverage tool (tools/sig_coverage/coverage.rb) on a small
# tree of its own (test/fixture): a few signatures, two apps, an app under
# flash/app/test, a template, a gem's mrblib and C file and an upstream C
# file. Each count is checked name by name, so a change in how the tool
# counts shows up here before it moves the real numbers.
#
#   ruby tools/sig_coverage/test/run.rb   (or: rake sig:test)

require "json"
require "stringio"
require "tmpdir"
require_relative "../coverage"

module Check
  @failed = 0
  def self.failed = @failed
  def self.fail! = @failed += 1
end

def check(what, got, want)
  if got == want
    puts "  ok   #{what}"
  else
    puts "  FAIL #{what}: got #{got.inspect}, want #{want.inspect}"
    Check.fail!
  end
end

FIXTURE = File.join(__dir__, "fixture")
layout = SigCoverage::Layout.new(FIXTURE)
analysis = SigCoverage::Analysis.new(layout, ignore_path: File.join(FIXTURE, "ignore.yml"))
items = analysis.items
r = analysis.result

puts "1. used by the apps"
check "app files (flash/app/test and the ignored file left out)",
      analysis.apps.files.map { |f| f.delete_prefix("#{FIXTURE}/") },
      %w[flash/app/demo/demo.app.rb flash/lib/templates/template.rb]
check "classes neither in sig/ nor the apps' own", items["app_class"], %w[Bar Clock FmrbConst Machine Math Net::HTTP]
check "constants of a missing class (Net::HTTP called like a class is a class)", items["app_constant"], %w[FmrbConst::KEY_UP Math::PI]
check "methods on a known receiver (class in sig/, missing class, @gfx)", items["app_method"],
      %w[Bar.new Clock.now FmrbGfx#nope Foo.vanish Machine.board_millis Net::HTTP.get_response]
check "uses are counted", r[:app_method].items["Machine.board_millis"][:count], 2
check "names on an untyped receiver that no class has (#roll ignored, size known)",
      items["app_name"], %w[/ byteslice each freeze no_such_method]
check "names without a receiver (helper, speed are the app's)", items["app_bare"], %w[new random_int send]
check "called on the class, declared on the instance", r[:app_kind].keys, %w[Foo.bar]
check "a library class is listed, not counted", r[:app_support].keys, %w[LibThing]
check "dynamic calls", analysis.apps.dynamic.keys, %w[send]

puts "2. implemented"
check "Ruby: public, not _, not after private, initialize as new, attr_reader",
      items["impl_ruby"], %w[Foo#count Foo.built? Foo.new Foo.still_public Tool.go Tool.stop]
check "Ruby: @api private is left out and told apart", r[:impl_api_private].map(&:key), %w[Foo#wired]
check "Ruby: an ignored method is not counted", items["impl_ruby"].include?("Foo#qux"), false
check "C: _id forms, nested class, kernel_module, upstream filter, comments skipped",
      items["impl_c"], %w[Foo#ready? Foo.new Foo::Widget#spin Kernel#beep Machine.board_millis Tool.c_go]
check "C: a target the file does not assign is unresolved",
      r[:impl_unresolved], ['lib/gems/picoruby-fmrb-demo/src/demo.c:22 helper_class(mrb) "lost"']

puts "3. doc gaps (aliases and private methods are not counted)"
check "methods checked", r[:doc][:methods], 6
check "no summary", items["doc_no_summary"], %w[Foo#no_doc]
check "summary over 112 bytes", items["doc_long_summary"], %w[Foo#long_summary]
check "no <<en>>", items["doc_no_en"], %w[Foo#no_doc Foo#summary_only]
check "no long form", items["doc_no_long"], %w[Foo#no_doc Foo#summary_only]
check "no example", items["doc_no_example"], %w[Foo#long_summary Foo#no_doc Foo#summary_only]

puts "ignore.yml and the baseline"
check "ignored: one name in 1 (#roll), one method in 2 (Foo#qux)",
      analysis.ignored_counts.to_h, { 1 => 1, 2 => 1 }
check "entries that match nothing", analysis.ignore.unused.map(&:name), %w[Nowhere]
baseline = { "items" => items.merge("app_class" => %w[Bar Clock Machine Math Net::HTTP Gone]) }
diff = SigCoverage::Report.new(analysis, baseline:).diff
check "baseline diff: new and gone", diff["app_class"], { added: %w[FmrbConst], removed: %w[Gone] }
check "baseline diff: unchanged category", diff["impl_c"], { added: [], removed: [] }

Dir.mktmpdir do |dir|
  out = File.join(dir, "report.md")
  base = File.join(dir, "baseline.json")
  status = nil
  capture = StringIO.new
  $stdout = capture
  begin
    status = SigCoverage.main(["--root", FIXTURE, "--ignore", File.join(FIXTURE, "ignore.yml"),
                               "--baseline", base, "--report", out, "--update"])
  ensure
    $stdout = STDOUT
  end
  check "main exits 0 (report only)", status, 0
  check "main writes the report", File.exist?(out), true
  check "main writes the baseline", JSON.parse(File.read(base))["items"]["app_class"], %w[Bar Clock FmrbConst Machine Math Net::HTTP]
  check "summary names the counts", capture.string.include?("classes 6"), true
end

if Check.failed.zero?
  puts "sig coverage tool: all checks passed"
else
  puts "sig coverage tool: #{Check.failed} check(s) FAILED"
  exit 1
end
