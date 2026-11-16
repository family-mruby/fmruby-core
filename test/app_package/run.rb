#!/usr/bin/env ruby
# Host tests for FmrbAppPackage (flash/usr/share/transfer/app_package.rb,
# doc/wasm_apps W6): the package an app travels in (a tar of its folder),
# as the machines write it (pack), as the PC tool writes it
# (tools/fmrb_send.rb, CRuby's Gem::Package::TarWriter) and as tar writes
# it (a folder of its own name, GNU tar); unpacking it beside /app/usr and
# what is refused (a way out of the folder, a link, a hidden file, two
# programs or none, no .app.toml for a wasm app, one that is not TOML, a
# wasm app the firmware's check refuses -- a stand-in here; the firmware's
# own is FmrbApp._wasm_check, run by regress:transfer:quick's install
# case); placing it (the icon of the package written as its path there,
# the installed one replaced, the launcher's cache dropped).
#
# The shipped file runs here with /app/usr and /var/cache moved into a
# temporary directory; FmrbToml is the firmware's reader
# (test/toml/host_toml.rb), Asterism::Transfer.file_name? the gem's
# (vendor/asterism, ASTERISM_DIR).
#
#   ruby test/app_package/run.rb      (rake app_package:test, in rake test)

require "tmpdir"
require "fileutils"
require "open3"
require_relative "../toml/host_toml"

ROOT = File.expand_path("../..", __dir__)
gem_dir = [ENV["ASTERISM_DIR"], File.join(ROOT, "vendor", "asterism")].compact.find do |d|
  File.exist?(File.join(d, "data", "transfer", "transfer.rb"))
end
abort "app_package: no asterism gem (rake asterism:setup, or ASTERISM_DIR=)" unless gem_dir
load File.join(gem_dir, "data", "transfer", "transfer.rb")

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

def raises(what, pattern)
  yield
  puts "  FAIL #{what}: no error"
  Check.fail!
rescue ArgumentError => e
  ok = e.message.match?(pattern)
  puts ok ? "  ok   #{what}" : "  FAIL #{what}: #{e.message}"
  Check.fail! unless ok
end

module Log
  def self.info(_m) = nil
  def self.warn(m) = puts("  (log) #{m}")
end

# The firmware's check of a wasm app's imports, played here: a module whose
# bytes hold "bad_import" is refused as the firmware words it.
class FmrbApp
  def self._wasm_check(path)
    b = File.binread(path)
    return [false, "", "#{path} is not a wasm module"] unless b.start_with?("\0asm")
    if b.include?("bad_import")
      return [false, "9.1.0", "imports fmrb.bad_import, which this firmware does not have: " \
                              "the app was built with SDK 9.1.0, newer than this firmware; update the firmware"]
    end
    [true, "0.1.0", ""]
  end
end

TMP = Dir.mktmpdir("fmrb_app_package_test")
at_exit { FileUtils.rm_rf(TMP) }
USR = "#{TMP}/app/usr"
CACHE = "#{TMP}/launcher_index"
FileUtils.mkdir_p(USR)
src = File.read("#{ROOT}/flash/usr/share/transfer/app_package.rb")
          .gsub("\"/app/usr\"", "\"#{USR}\"").gsub("/var/cache/launcher_index", CACHE)
eval(src, TOPLEVEL_BINDING, "app_package.rb")
P = FmrbAppPackage
load File.join(ROOT, "tools", "fmrb_send.rb")

WASM = "\0asm\x01\0\0\0".b
TOML = "app_screen_name = \"Hello\"\nlanguage = \"tinygo\"\nicon = \"hello.icon\"\n"

def folder(name, files)
  d = "#{TMP}/src/#{name}"
  FileUtils.rm_rf(d)
  files.each do |rel, data|
    FileUtils.mkdir_p(File.dirname("#{d}/#{rel}"))
    File.binwrite("#{d}/#{rel}", data)
  end
  d
end

def stage(tar, name)
  st = P.stage_of(name)
  got = P.unpack(tar, st)
  [st, got]
end

puts "the machines' pack, unpacked"
d = folder("hello", { "hello.app.wasm" => WASM + "x" * 700, "hello.app.toml" => TOML, "hello.png" => "PNG" * 40,
                      "data/level1.bin" => "L" * 1000 })
tar = "#{TMP}/hello.tar"
r = P.pack(d, tar)
check("pack: the name", r["name"], "hello")
check("pack: four files", r["files"], 4)
check("pack: whole blocks", File.size(tar) % 512, 0)
out, st = Open3.capture2e("tar", "tf", tar)
check("GNU tar reads it", st.success? && out.split("\n").sort, %w[data/level1.bin hello.app.toml hello.app.wasm hello.png])
s, got = stage(tar, "hello")
check("unpacked: four files", got["files"].size, 4)
check("the subfolder's file", File.binread("#{s}/data/level1.bin"), "L" * 1000)
check("the program's bytes", File.binread("#{s}/hello.app.wasm"), WASM + "x" * 700)
info = P.check(s, "hello", got["files"])
check("the program", info["main"], "hello.app.wasm")
check("the language", info["lang"], "wasm (TinyGo)")
check("the label", info["label"], "Hello")
check("the SDK version", info["sdk"], "0.1.0")
check("the bytes", info["bytes"], 700 + 8 + TOML.bytesize + 120 + 1000)
File.write(CACHE, "x")
path = P.place(s, info)
check("placed", path, "#{USR}/hello/hello.app.wasm")
check("the hidden folder is gone", Dir.exist?(s), false)
check("installed?", P.installed?("hello"), true)
check("the launcher's cache dropped", File.exist?(CACHE), false)
check("the icon written as its path there",
      File.read("#{USR}/hello/hello.app.toml").include?("icon = \"app/usr/hello/hello.icon\""), true)
check("the other keys kept", File.read("#{USR}/hello/hello.app.toml").include?("language = \"tinygo\""), true)

puts "a program and the files of its name (pack)"
d2 = folder("two", { "game.app.rb" => "puts 1\n", "game.app.toml" => "app_screen_name = \"Game\"\n",
                     "game.png" => "P", "other.txt" => "not this" })
r = P.pack("#{d2}/game.app.rb", tar)
check("the name from the program", r["name"], "game")
check("three files (not other.txt)", r["files"], 3)
s, got = stage(tar, "game")
info = P.check(s, "game", got["files"])
check("Ruby", info["lang"], "Ruby")
P.remove_tree(s)

puts "the PC tool's package (Gem::Package::TarWriter)"
FmrbSend.pack_app(d, tar)
s, got = stage(tar, "hello")
info = P.check(s, "hello", got["files"])
check("read", [info["main"], got["files"].size], ["hello.app.wasm", 4])
path = P.place(s, info)
check("replaced the installed one", File.exist?(path) && Dir.children("#{USR}/hello").size, 4)
name, = FmrbSend.app_files("#{d2}/game.app.rb")
check("the PC tool: the name from the program", name, "game")

puts "tar's own package of the folder (every entry under hello/)"
Dir.chdir("#{TMP}/src") { system("tar", "cf", tar, "hello") or abort("tar failed") }
s, got = stage(tar, "hello")
info = P.check(s, "hello", got["files"])
check("lifted out of hello/", info["main"], "hello.app.wasm")
check("the subfolder lifted too", File.exist?("#{s}/data/level1.bin"), true)
P.remove_tree(s)

puts "refused"
def tar_of(entries)
  require "rubygems/package"
  t = "#{TMP}/bad.tar"
  File.open(t, "wb") do |io|
    Gem::Package::TarWriter.new(io) do |w|
      entries.each do |n, data, kind|
        if kind == :link
          w.add_symlink(n, data, 0o777)
        else
          w.add_file_simple(n, 0o644, data.bytesize) { |f| f.write(data) }
        end
      end
    end
  end
  t
end
raises("a way out of the folder", /not a name in the app's folder/) do
  stage(tar_of([["../evil.app.rb", "x"]]), "evil")
end
check("nothing left beside /app/usr", Dir.children(USR).sort, ["hello"])
raises("a link", /not a file or a folder/) { stage(tar_of([["x.app.rb", "/etc/passwd", :link]]), "x") }
raises("a hidden file", /not a name in the app's folder/) { stage(tar_of([[".hidden", "x"]]), "x") }
raises("an absolute path", /starts with \//) { stage(tar_of([["/abs.app.rb", "x"]]), "abs") }
raises("not a tar", /not a tar/) do
  File.binwrite("#{TMP}/junk.tar", "hello" * 200)
  stage("#{TMP}/junk.tar", "junk")
end
raises("a tar with nothing in it", /holds no file/) do
  File.binwrite("#{TMP}/empty.tar", "\0" * 1024)
  stage("#{TMP}/empty.tar", "empty")
end
s, got = stage(tar_of([["a.app.rb", "1"], ["a.app.lua", "2"]]), "a")
raises("two programs", /two programs/) { P.check(s, "a", got["files"]) }
P.remove_tree(s)
s, got = stage(tar_of([["b.txt", "1"]]), "b")
raises("no program", /no b.app.rb/) { P.check(s, "b", got["files"]) }
P.remove_tree(s)
s, got = stage(tar_of([["c.app.wasm", WASM]]), "c")
raises("a wasm app with no .app.toml", /no c.app.toml/) { P.check(s, "c", got["files"]) }
P.remove_tree(s)
s, got = stage(tar_of([["d.app.lua", "x"], ["d.app.toml", "this is = = not toml"]]), "d")
raises("an .app.toml that is not TOML", /not TOML/) { P.check(s, "d", got["files"]) }
P.remove_tree(s)
s, got = stage(tar_of([["e.app.wasm", "#{WASM}bad_import"], ["e.app.toml", "language = \"rust\"\n"]]), "e")
raises("a wasm app the firmware refuses (with the SDK version)", /e.app.wasm: imports fmrb.bad_import.*SDK 9.1.0/) do
  P.check(s, "e", got["files"])
end
P.remove_tree(s)
s, got = stage(tar_of([["f.app.rb", "#---fmrb\n"]]), "f")
check("a Ruby app without .app.toml is taken", P.check(s, "f", got["files"])["lang"], "Ruby")
P.remove_tree(s)
check("names", [P.name_ok?("ok_1-x"), P.name_ok?("a.b"), P.name_ok?(""), P.name_ok?("x" * 41)],
      [true, false, false, false])

puts "left from an install that did not end"
FileUtils.mkdir_p("#{USR}/.zz.new/sub")
File.write("#{USR}/.zz.new/sub/f", "x")
P.sweep
check("swept", Dir.children(USR).sort, ["hello"])

puts(Check.failed.zero? ? "app_package: all ok" : "app_package: #{Check.failed} FAILED")
exit(Check.failed.zero? ? 0 : 1)
