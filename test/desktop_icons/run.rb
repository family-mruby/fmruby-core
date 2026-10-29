#!/usr/bin/env ruby
# Host tests for the desktop icon list (doc/linux_native L15b).
#
# Which icons the desktop shows is read from /home/desktop.toml (apps and
# hidden) merged with the default set in /usr/share/desktop/desktop.toml. That
# part of main/prebuild_scripts/kernel/system_desktop/desktop_icons.rb is
# plain Ruby over two files, so the shipped mixin runs here with its two paths
# pointed at a scratch directory and the drawing side stubbed out. What this
# cannot see is the icons on the screen; the simulator shows those.
#
#   ruby test/desktop_icons/run.rb   (or: rake desktop_icons:test)

require "tmpdir"
require "fileutils"

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

# What the mixin reaches for outside itself, played by the smallest stand-ins.
module Log
  def self.info(_m); end
  def self.warn(_m); end
end

module FmrbConst
  THEME_WINDOW_BG = 0xFF
  THEME_HIGHLIGHT = 0x1F
  THEME_TEXT = 0x00
end

ROOT = File.expand_path("../..", __dir__)
load File.join(ROOT, "main/prebuild_scripts/kernel/system_desktop/desktop_icons.rb")

DIR = Dir.mktmpdir("desktop_icons")
at_exit { FileUtils.rm_rf(DIR) }
USER = File.join(DIR, "home_desktop.toml")
DEFAULT = File.join(DIR, "share_desktop.toml")
DesktopIconsMixin.send(:remove_const, :DICON_USER_PATH)
DesktopIconsMixin.send(:remove_const, :DICON_DEFAULT_PATH)
DesktopIconsMixin.const_set(:DICON_USER_PATH, USER)
DesktopIconsMixin.const_set(:DICON_DEFAULT_PATH, DEFAULT)

class Desk
  include DesktopIconsMixin
  attr_reader :dicon_paths, :dicon_hidden

  # Resolving and drawing are the screen's half; only the lists are under test.
  def resolve_desktop_icons; end
  def draw_background; end
end

def write_default(list)
  File.write(DEFAULT, "# default\napps = [\n" + list.map { |p| "  \"#{p}\",\n" }.join + "]\n")
end

def boot
  d = Desk.new
  d.load_desktop_icon_paths
  d
end

def user_lists
  text = File.read(USER)
  d = Desk.new
  [d.parse_desktop_icon_list(text, "apps"), d.parse_desktop_icon_list(text, "hidden")]
end

def reset
  FileUtils.rm_f(USER)
  FileUtils.rm_f(DEFAULT)
end

puts "no user file"
reset
write_default(%w[a b c])
d = boot
check("the default set is shown", d.dicon_paths, %w[a b c])
check("nothing hidden", d.dicon_hidden, [])
check("booting does not create the file", File.exist?(USER), false)

puts "the menu writes apps and hidden"
d.toggle_desktop_icon("b")
check("removed: shown", d.dicon_paths, %w[a c])
check("removed: hidden", d.dicon_hidden, %w[b])
check("the file is created", File.exist?(USER), true)
check("the file holds both lists", user_lists, [%w[a c], %w[b]])
d.toggle_desktop_icon("x")
check("added at the end", d.dicon_paths, %w[a c x])
d.toggle_desktop_icon("b")
check("added back: out of hidden", [d.dicon_paths, d.dicon_hidden], [%w[a c x b], []])
check("the file follows", user_lists, [%w[a c x b], []])
d.toggle_desktop_icon("a")
check("removed again", user_lists, [%w[c x b], %w[a]])

puts "a new default is appended, a hidden one does not come back"
write_default(%w[a b c n1 n2])
before = File.read(USER)
d = boot
check("shown", d.dicon_paths, %w[c x b n1 n2])
check("hidden kept", d.dicon_hidden, %w[a])
check("written back", user_lists, [%w[c x b n1 n2], %w[a]])
check("the file changed", File.read(USER) != before, true)
before = File.read(USER)
d = boot
check("next boot: same lists", [d.dicon_paths, d.dicon_hidden], [%w[c x b n1 n2], %w[a]])
check("next boot: the file is not rewritten", File.read(USER), before)

puts "a hidden default stays off after the default set changes order"
write_default(%w[n2 a c])
d = boot
check("shown", d.dicon_paths, %w[c x b n1 n2])
check("hidden", d.dicon_hidden, %w[a])

puts "the form without hidden is replaced by the default set"
write_default(%w[a b c])
File.write(USER, "apps = [\"z\", \"a\"]\n")
d = boot
check("shown", d.dicon_paths, %w[a b c])
check("nothing hidden", d.dicon_hidden, [])
check("written in the new form", user_lists, [%w[a b c], []])

puts "an empty hidden list is the new form"
File.write(USER, "apps = [\"z\", \"a\"]\nhidden = []\n")
d = boot
check("shown: the user's, plus the new defaults", d.dicon_paths, %w[z a b c])
check("hidden", d.dicon_hidden, [])

puts "hidden before apps, over several lines, with comments"
File.write(USER, "# mine\nhidden = [\n  \"b\", # off\n]\napps = [\n  \"c\",\n  \"a\",\n]\n")
d = boot
check("shown", d.dicon_paths, %w[c a])
check("hidden", d.dicon_hidden, %w[b])

puts "a broken user file counts as absent"
File.write(USER, "apps = [\"a\", \"b\n")
before = File.read(USER)
d = boot
check("the default set is shown", d.dicon_paths, %w[a b c])
check("the file is left alone", File.read(USER), before)

puts "no default set"
FileUtils.rm_f(DEFAULT)
File.write(USER, "apps = [\"z\"]\n")
before = File.read(USER)
d = boot
check("the old form is kept as it is", [d.dicon_paths, File.read(USER)], [%w[z], before])
FileUtils.rm_f(USER)
d = boot
check("no file at all: no icons", d.dicon_paths, [])

if Check.failed > 0
  puts "desktop icons: #{Check.failed} FAILED"
  exit 1
end
puts "desktop icons: all passed"
