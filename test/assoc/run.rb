#!/usr/bin/env ruby
# Host tests for FmrbAssoc (lib/add/picoruby-fmrb-app/mrblib/fmrb-assoc.rb).
#
# The table that says which app opens which file. It is plain Ruby over
# Strings and two file reads, so the shipped file runs here with only those
# reads replaced -- the parsing, the two layers and the resolution under test
# are the real ones. Same tier and reasoning as test/fmrb_ui/run.rb: no
# docker, no firmware, no device.
#
# What this cannot see: whether the file manager and the shell act on the
# answer. That stays with the simulator (doc/user_extension/assoc/report/a1.md).
#
#   ruby test/assoc/run.rb        (or: rake assoc:test)

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

# --- file associations (A1) -----------------------------------------------
#
# FmrbAssoc is a gem file (lib/add/picoruby-fmrb-app/mrblib), so the real one
# runs here with only its two file reads replaced -- the parsing, the two
# layers and the resolution are the shipped code.

load File.expand_path("../../lib/add/picoruby-fmrb-app/mrblib/fmrb-assoc.rb", __dir__)

module FmrbAssoc
  class << self
    attr_accessor :fake_sys, :fake_usr
    def read_file(path)
      path == SYS_PATH ? @fake_sys : @fake_usr
    end
  end
end

def assoc_tables(sys, usr)
  FmrbAssoc.fake_sys = sys
  FmrbAssoc.fake_usr = usr
  FmrbAssoc.forget
end

SYS_TABLE = <<~TOML
  # the shipped table
  rb  = "run"
  md  = "/app/tool/picorabbit.app.rb"
  txt = "edit"
  nsf = "/app/tool/nsf_player.app.rb"   # trailing comment
TOML

assoc_tables(SYS_TABLE, nil)
check("a program runs itself", FmrbAssoc.resolve("/home/sample/hello.rb"), "run")
check("a deck opens its app", FmrbAssoc.resolve("/home/slides/demo.md"),
      "/app/tool/picorabbit.app.rb")
check("text goes to the editor", FmrbAssoc.resolve("/home/notes.txt"), "edit")
check("a trailing comment is not part of the path",
      FmrbAssoc.resolve("/home/music/dq3.nsf"), "/app/tool/nsf_player.app.rb")
# Anything unlisted is shown rather than ignored.
check("an unlisted extension defaults to the editor",
      FmrbAssoc.resolve("/home/thing.xyz"), "edit")
check("so does no extension at all", FmrbAssoc.resolve("/home/README"), "edit")
# A dot in a directory name is not the file's extension.
check("a dot in a directory is not an extension",
      FmrbAssoc.resolve("/home/my.decks/README"), "edit")
check("an empty extension is not one", FmrbAssoc.resolve("/home/odd."), "edit")

# A table is written in lower case; a file need not be.
check("the extension is matched without case",
      FmrbAssoc.resolve("/home/slides/DEMO.MD"), "/app/tool/picorabbit.app.rb")
check("and mixed case too", FmrbAssoc.resolve("/home/Notes.Txt"), "edit")

# The user's table wins per extension, and leaves the rest of the system one
# alone -- that is what makes "open .md in the editor instead" a one-line file.
assoc_tables(SYS_TABLE, "md = \"edit\"\n")
check("the user overrides one extension", FmrbAssoc.resolve("/home/a.md"), "edit")
check("and the others are untouched", FmrbAssoc.resolve("/home/a.rb"), "run")
assoc_tables(SYS_TABLE, "odt = \"/app/tool/writer.app.rb\"\n")
check("a user may add an extension", FmrbAssoc.resolve("/home/a.odt"),
      "/app/tool/writer.app.rb")

# A hand-written table has junk in it; a bad line costs its own line only.
assoc_tables("rb = \"run\"\nno equals here\n= nothing\nmd =\ntxt = \"edit\"\n", nil)
check("junk lines are skipped", FmrbAssoc.resolve("/home/a.rb"), "run")
check("and the lines after them still parse", FmrbAssoc.resolve("/home/a.txt"), "edit")
check("an empty value is not an answer", FmrbAssoc.resolve("/home/a.md"), "edit")

# No tables at all: everything opens in the editor rather than nothing
# happening.
assoc_tables(nil, nil)
check("with no table, everything is editable", FmrbAssoc.resolve("/home/a.md"), "edit")

puts(Check.failed.zero? ? "assoc: all checks passed" : "assoc: #{Check.failed} FAILED")
exit(Check.failed.zero? ? 0 : 1)
