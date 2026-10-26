#!/usr/bin/env ruby
# Host tests for FmrbToml (lib/add/picoruby-fmrb-app/mrblib/fmrb-toml.rb and
# the walk in ports/esp32/toml_walk.c): reading through tomlc99, and update,
# which rewrites only the lines of the keys it is given.
#
# What this cannot see: the mruby objects toml.c builds and the per-VM
# allocator. Those are checked in the simulator (doc/archive/app_refactor/report/r2.md).
#
#   ruby test/toml/run.rb        (or: rake toml:test)

require "tmpdir"
require_relative "host_toml"

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

puts "reading"
h = FmrbToml.parse(<<~TOML)
  # comment
  name = "a \\"quoted\\" word\\n"   # trailing comment
  n = 0xFC
  neg = -5
  big = 1_000
  f = 1.5
  yes = true
  bare = black
  when = 1979-05-27
  list = [1, "two", [3]]

  [s]
  k = 'literal \\n'
  [s.sub]
  deep = 1

  [[mark]]
  step = 0
  text = "Walk"
  [[mark]]
  step = 10
TOML
check("a string with escapes", h["name"], "a \"quoted\" word\n")
check("hex integer", h["n"], 252)
check("negative", h["neg"], -5)
check("underscores", h["big"], 1000)
check("float", h["f"], 1.5)
check("boolean", h["yes"], true)
check("an unquoted word is its text", h["bare"], "black")
check("a date is its text", h["when"], "1979-05-27")
check("a mixed array", h["list"], [1, "two", [3]])
check("a literal string keeps its backslash", h["s"]["k"], "literal \\n")
check("a sub-table nests", h["s"]["sub"]["deep"], 1)
check("an array of tables", h["mark"], [{ "step" => 0, "text" => "Walk" }, { "step" => 10 }])
# tomlc99 keeps values, arrays and tables apart: file order within each kind.
check("keys in file order, by kind", h.keys, ["name", "n", "neg", "big", "f", "yes", "bare", "when", "list", "mark", "s"])
check("UTF-8 survives", FmrbToml.parse("t = \"\u6b69\u304f\"\n")["t"], "\u6b69\u304f")
check("empty text is an empty table", FmrbToml.parse(""), {})

puts "errors"
check("a line without = is an error", FmrbToml.parse("a = 1\noops\n"), nil)
check("and error says where", FmrbToml.error, "line 2: missing =")
check("a duplicate key is an error", FmrbToml.parse("a = 1\na = 2\n"), nil)
check("two words are an error", FmrbToml.parse("a = hello world\n"), nil)
check("an empty value is an error", FmrbToml.parse("md =\n"), nil)
check("an unclosed table is an error", FmrbToml.parse("[good\n"), nil)
check("a NUL is an error, not a raise", FmrbToml.parse("a = 1\0"), nil)

Dir.mktmpdir do |dir|
  path = File.join(dir, "x.toml")
  puts "load"
  Log.lines.clear
  check("a missing file is nil", FmrbToml.load(path), nil)
  check("quietly", Log.lines, [])
  File.write(path, "a = 1\nb\n")
  check("a broken file is nil", FmrbToml.load(path), nil)
  check("with one log line", Log.lines, ["toml: #{path}: line 2: missing ="])
  File.write(path, "")
  check("an empty file is an empty table", FmrbToml.load(path), {})

  puts "update"
  File.unlink(path)
  r = FmrbToml.update(path, { "rise_mg" => 130, "min_gap_ms" => 250 })
  check("a new file", File.read(path), "rise_mg = 130\nmin_gap_ms = 250\n")
  check("returns what it reads back", r, { "rise_mg" => 130, "min_gap_ms" => 250 })

  File.write(path, <<~TOML)
    # tuned by hand
    rise_mg = 150   # was 150
    other = "keep"

    [editor]
    # the editor's colours
    bg = "navy"
    [shell]
    bg = black
  TOML
  FmrbToml.update(path, { "rise_mg" => 140, "added" => true, "editor.text" => 0xFC,
                          "shell.bg" => FmrbToml.raw("white"), "game.level" => 2 })
  check("only the given keys change", File.read(path), <<~TOML)
    # tuned by hand
    rise_mg = 140
    other = "keep"
    added = true

    [editor]
    # the editor's colours
    bg = "navy"
    text = 252
    [shell]
    bg = white

    [game]
    level = 2
  TOML
  check("nil removes a key", FmrbToml.update(path, { "editor.bg" => nil })["editor"], { "text" => 252 })
  check("a top-level key goes before the first table",
        FmrbToml.update(path, { "z" => "s" })["z"], "s")
  check("and stays top-level", File.read(path).index("z = \"s\"") < File.read(path).index("[editor]"), true)

  File.write(path, "[a]\nx = 1\n")
  FmrbToml.update(path, { "top" => 1 })
  check("top-level key into a file that starts with a table", File.read(path), "top = 1\n[a]\nx = 1\n")

  File.write(path, "a = 1\noops\n")
  check("a broken file is not written", FmrbToml.update(path, { "a" => 2 }), nil)
  check("and is left as it was", File.read(path), "a = 1\noops\n")

  # /home/mahjong.toml as the code before R2 wrote it (the whole file), then
  # rewritten line by line by the code after it.
  File.write(path, "# The mahjong table's help (0 none, 1 riichi, 2 tenpai, 3 shanten)\nassist = 2\n" \
                   "# The name shown at the table (none: the machine's)\nname = \"\u3072\u3067\"\n" \
                   "# Sound: the BGM's level (0 off, 1 low, 2 mid, 3 high), the effects and the voices on (true) or off (false)\n" \
                   "bgm = 1\nse = true\nvoice = false\nbgm_play = \"/home/music/a.nsf\"\nbgm_play_track = 2\n")
  old = FmrbToml.load(path)
  check("an old mahjong.toml reads", old, { "assist" => 2, "name" => "\u3072\u3067", "bgm" => 1, "se" => true,
                                            "voice" => false, "bgm_play" => "/home/music/a.nsf", "bgm_play_track" => 2 })
  FmrbToml.update(path, { "assist" => 3, "name" => nil, "bgm" => 2, "se" => true, "voice" => true,
                          "bgm_play" => "/home/music/a.nsf", "bgm_play_track" => 2 })
  check("and keeps its comments when written again", File.read(path),
        "# The mahjong table's help (0 none, 1 riichi, 2 tenpai, 3 shanten)\nassist = 3\n" \
        "# The name shown at the table (none: the machine's)\n" \
        "# Sound: the BGM's level (0 off, 1 low, 2 mid, 3 high), the effects and the voices on (true) or off (false)\n" \
        "bgm = 2\nse = true\nvoice = true\nbgm_play = \"/home/music/a.nsf\"\nbgm_play_track = 2\n")

  File.unlink(path)
  v = { "s" => "q\"b\\\n\t", "f" => 2.0, "l" => [1, "a"], "t" => { "k" => 1, "a b" => "c" }, "sym" => :x }
  r = FmrbToml.update(path, v)
  check("values round-trip", r, { "s" => "q\"b\\\n\t", "f" => 2.0, "l" => [1, "a"],
                                  "t" => { "k" => 1, "a b" => "c" }, "sym" => "x" })
end

puts "dump_value"
check("string", FmrbToml.dump_value("a\"b"), "\"a\\\"b\"")
check("control character", FmrbToml.dump_value("\x01"), "\"\\u0001\"")
check("float", FmrbToml.dump_value(3.0), "3.0")
check("raw", FmrbToml.dump_value(FmrbToml.raw("black")), "black")

puts(Check.failed.zero? ? "toml: all checks passed" : "toml: #{Check.failed} FAILED")
exit(Check.failed.zero? ? 0 : 1)
