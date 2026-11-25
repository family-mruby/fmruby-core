#!/usr/bin/env ruby
# frozen_string_literal: true

# Tests of the class reference generator (tools/ti/gen_site.rb) on a small
# sig/ and site of its own (test_site/fixture): the shape of a page, the
# exclude list, the links in signatures, the guide lines and nav entries,
# that a second run changes nothing, and that only the tool's own files are
# removed.
#
#   ruby tools/ti/test_site/run.rb   (or: rake ti:site:test)

require "fileutils"
require "stringio"
require "tmpdir"
require_relative "../gen_site"

module Check
  @failed = 0
  def self.failed = @failed
  def self.fail! = @failed += 1
end

def check(what, ok, detail = nil)
  if ok
    puts "  ok   #{what}"
  else
    puts "  FAIL #{what}#{detail ? ": #{detail}" : ''}"
    Check.fail!
  end
end

def snapshot(dir)
  Dir.glob("**/*", File::FNM_DOTMATCH, base: dir).sort
     .reject { |f| File.directory?(File.join(dir, f)) }
     .to_h { |f| [f, File.read(File.join(dir, f))] }
end

FIXTURE = File.join(__dir__, "fixture")

Dir.mktmpdir("gen_site_test") do |tmp|
  site = File.join(tmp, "site")
  FileUtils.cp_r(File.join(FIXTURE, "site"), site)
  ref = File.join(site, "docs/api/reference")
  FileUtils.mkdir_p(ref)
  File.write(File.join(ref, "hand.md"), "written by hand\n")
  warn_io = StringIO.new
  run = lambda do |only = nil|
    TiSite.generate(sig_dir: File.join(FIXTURE, "sig"), site_dir: site, only: only, version: "test@0000000",
                    exclude_path: File.join(FIXTURE, "exclude.yml"), warn_io: warn_io)
  end

  puts "1. pages"
  r = run.call
  check "classes written (Hidden left out)", r.classes == 4 && r.excluded == ["Hidden"], r.to_h.inspect
  files = Dir.children(ref).sort
  check "one page per class and language, the index, the list",
        files == %w[.generated FmrbApp.ja.md FmrbApp.md FmrbUI.ja.md FmrbUI.md FmrbUI__Button.ja.md
                    FmrbUI__Button.md FmrbUI__Widget.ja.md FmrbUI__Widget.md hand.md index.ja.md index.md],
        files.inspect
  en = File.read(File.join(ref, "FmrbUI.md"))
  ja = File.read(File.join(ref, "FmrbUI.ja.md"))
  check "English summary and long form", en.include?("Widget box\n") && en.include?("The English long form.")
  check "Japanese summary and long form", ja.include?("Widget box (test)\n") && ja.include?("日本語の長文。")
  check "Japanese lines of a paragraph joined, not around ASCII or in code",
        ja.include?("日本語の長文。obj\nに渡す。") && ja.include?("# 日本語の\n# コメント")
  check "a heading per method", en.include?("### FmrbUI.new\n") && en.include?("### FmrbUI#button\n")
  check "constants and types as tables", en.include?(%(| <span class="fmrb-name">`DEFAULT`</span> | `Integer` | default colour |)) &&
                                          en.include?(%(| <span class="fmrb-name">`size`</span> | `{ w: Integer, h: Integer }` | A size |))
  check "the example kept as a code block", en.include?("```ruby\nFmrbUI.new(self)\n```")
  check "footer names the version", en.include?("fmruby-core test@0000000.") && ja.include?("の test@0000000 の")

  puts "2. signatures"
  check "a class in a signature links to its page (nested name resolved)",
        en.include?(%(-&gt; <a href="../FmrbUI__Button/">Button</a>))
  check "a class with a page linked from an argument", en.include?(%(<a href="../FmrbApp/">FmrbApp</a> app))
  check "a left-out class is not linked", en.include?("-&gt; Hidden?") && !en.include?("Hidden/")
  check "the page's own class is not linked", !en.include?(%(href="../FmrbUI/"))
  check "a long signature one parameter to a line",
        en.include?("button(\n  Symbol id,\n  Integer x,") && en.include?("  ?accent: Integer\n) -&gt; <a")
  check "every overload on its own line",
        en.include?("find(Symbol id) -&gt; <a href=\"../FmrbUI__Button/\">Button</a>\nfind(Integer index)")
  btn = File.read(File.join(ref, "FmrbUI__Button.md"))
  check "superclass linked", btn.include?("Inherits from: [`Widget`](FmrbUI__Widget.md)")

  puts "3. index, nav, guides"
  idx = File.read(File.join(ref, "index.md"))
  check "index groups by area, unknown file under Other",
        idx.index("## Drawing") && idx.index("## Other") && idx.index("## Drawing") < idx.index("## Other")
  check "index has no notice box", !idx.include?("!!! note") && !File.read(File.join(ref, "index.ja.md")).include?("!!! note")
  check "index shows the first sentence of a wrapped one-language comment",
        idx.include?("| Every widget has a **box** and an id. | 0 |")
  wid = File.read(File.join(ref, "FmrbUI__Widget.md"))
  check "class page keeps the wrapped comment as one paragraph",
        wid.include?("# FmrbUI::Widget\n\nEvery widget has a **box** and\nan id. More text here.\n\n") &&
        wid.include?("Second paragraph.")
  check "index leaves out Hidden", !idx.include?("Hidden")
  check "warned about the file in no area", warn_io.string.include?("zz_extra.rbs is in no area")
  yml = File.read(File.join(site, "mkdocs.yml"))
  check "nav block under API Reference, before the next entry",
        yml.index("      - Class Reference:") < yml.index("  - Other page:") &&
        yml.include?(%(              - "FmrbUI::Button": api/reference/FmrbUI__Button.md))
  check "translations added, an existing one left alone",
        yml.include?("            Class Reference: クラスリファレンス\n") &&
        yml.include?("            Other: その他\n") && !yml.match?(/^ {12}Drawing: .*\n(?:.*\n)*^ {12}Drawing:/)
  ui = File.read(File.join(site, "docs/api/ui.md"))
  check "guide keeps its body and gets the line",
        ui.start_with?("# UI Widgets\n\nHand-written body.\n") &&
        ui.include?("**Class reference:** [`FmrbUI`](reference/FmrbUI.md), [`FmrbUI::Widget`](reference/FmrbUI__Widget.md)")
  check "class page points at its guide (nested class at its outer class's)",
        en.include?("How to use it: [UI Widgets](../ui.md).") && btn.include?("[UI Widgets](../ui.md)")
  check "overview guide links the index",
        File.read(File.join(site, "docs/api/index.md")).include?("[every class](reference/index.md)")
  check "missing guide pages are reported", warn_io.string.include?("no guide page")

  puts "4. a second run"
  before = snapshot(site)
  r2 = run.call
  check "changes nothing", snapshot(site) == before && r2.written.zero?,
        (snapshot(site).to_a - before.to_a).map(&:first).inspect
  check "guide block not doubled", File.read(File.join(site, "docs/api/ui.md")).scan(TiSite::GUIDE_BEGIN).size == 1

  puts "5. a smaller run removes only its own pages"
  r3 = run.call(%w[FmrbUI])
  left = Dir.children(ref).sort
  check "pages no longer made are removed, hand.md stays",
        left == %w[.generated FmrbUI.ja.md FmrbUI.md hand.md index.ja.md index.md], left.inspect
  check "removed list", r3.removed.sort == %w[FmrbApp.ja.md FmrbApp.md FmrbUI__Button.ja.md FmrbUI__Button.md
                                              FmrbUI__Widget.ja.md FmrbUI__Widget.md]
  check "links to pages no longer made are gone", !File.read(File.join(ref, "FmrbUI.md")).include?("<a href")
  check "guide line names only what is there",
        File.read(File.join(site, "docs/api/ui.md")).include?("**Class reference:** [`FmrbUI`](reference/FmrbUI.md)\n")
end

if Check.failed.zero?
  puts "gen_site tests: all passed"
else
  puts "gen_site tests: #{Check.failed} failed"
  exit 1
end
