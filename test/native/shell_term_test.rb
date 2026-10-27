# Host test of ShellTerm (main/prebuild_scripts/default_app/shell/shell_term.rb,
# doc/linux_native L10): a Linux program's bytes -> the Shell's lines and
# colour spans. CRuby runs the same file the Shell's mruby does.
#
# Usage: ruby test/native/shell_term_test.rb

require_relative "../../main/prebuild_scripts/default_app/shell/shell_term"

$fail = 0
$count = 0

def check(cond, msg)
  $count += 1
  return if cond
  $fail += 1
  puts "FAIL #{caller(1, 1)[0]}: #{msg}"
end

def lines_of(*chunks)
  t = ShellTerm.new
  chunks.each { |c| t.feed(c.b.force_encoding("UTF-8")) }
  t.finish
end

# attr as the Shell packs it
def at(fg = -1, bg = -1)
  (fg + 1) | ((bg + 1) << 5)
end

# Plain lines, CR LF, a last line without its newline.
l = lines_of("hello\r\nworld\r\nlast")
check(l == ["hello", "world", "last"], "plain: #{l.inspect}")

# Colours: ls --color=always style, and the reset.
l = lines_of("\e[0m\e[01;34mdir\e[0m  file\r\n")
check(l == [{ type: :ansi, text: "dir  file", spans: [[0, at(12)], [3, 0]] }],
      "bold blue then reset: #{l.inspect}")
l = lines_of("\e[31mred\e[32mgreen\e[39m plain\e[91m bright\e[0m\r\n")
check(l[0][:spans] == [[0, at(1)], [3, at(2)], [8, 0], [14, at(9)]],
      "8 and bright colours: #{l.inspect}")
l = lines_of("\e[30;42mx\e[49my\e[m z\r\n")
check(l[0][:spans] == [[0, at(0, 2)], [1, at(0)], [2, 0]], "background: #{l.inspect}")
l = lines_of("\e[38;5;3ma\e[38;5;200mb\e[38;2;1;2;3mc\e[0m\r\n")
check(l[0][:spans] == [[0, at(3)], [1, 0]], "256 / true colour: #{l.inspect}")
l = lines_of("\e[1mbold\e[22mnormal\r\n")
check(l == ["boldnormal"], "bold of the Shell's colour stays plain: #{l.inspect}")

# A sequence cut between two reads.
l = lines_of("a\e[3", "1mb\e", "[0mc\r\n")
check(l[0] == { type: :ansi, text: "abc", spans: [[1, at(1)], [2, 0]] }, "split sequence: #{l.inspect}")

# Cursor movement, erasing, titles: read and dropped.
l = lines_of("\e[2J\e[H\e[?25lok\e[K\e]0;title\a\e(B!\r\n")
check(l == ["ok!"], "dropped sequences: #{l.inspect}")

# \r rewrites the line (progress counters); backspace goes back one.
l = lines_of("10%\r20%\r100%\r\n")
check(l == ["100%"], "carriage return: #{l.inspect}")
l = lines_of("abc\b\bX\r\n")
check(l == ["aXc"], "backspace: #{l.inspect}")
l = lines_of("abc\b \b\r\n")
check(l == ["ab "], "erase echo: #{l.inspect}")
l = lines_of("\e[32mlong text\r\e[31mX\r\n")
check(l[0] == { type: :ansi, text: "Xong text", spans: [[0, at(1)], [1, at(2)]] },
      "rewrite keeps the colours of the rest: #{l.inspect}")

# Tabs to the next 8 columns.
l = lines_of("a\tb\r\n12345678\tc\r\n")
check(l == ["a       b", "12345678        c"], "tab: #{l.inspect}")

# UTF-8 cut in the middle of a character between reads.
s = "あい\r\n".b
l = lines_of(s[0, 4], s[4..])
check(l == ["あい"], "utf-8 split: #{l.inspect}")

# The partial line and its cursor while no newline came yet.
t = ShellTerm.new
t.feed("$ ")
check(t.partial_entry == "$ " && t.cursor == 2 && t.take_lines.empty?, "prompt: #{t.partial_entry.inspect}")
t.feed("\e[1;32mok\e[0m")
check(t.partial_entry.is_a?(Hash) && t.cursor == 4, "coloured partial: #{t.partial_entry.inspect}")

# Many lines: none lost.
t = ShellTerm.new
data = (1..2000).map { |i| "#{i}\r\n" }.join
got = []
data.bytes.each_slice(4096) { |sl| t.feed(sl.pack("C*").force_encoding("UTF-8")); got.concat(t.take_lines) }
check(got == (1..2000).map(&:to_s), "2000 lines: #{got.size}, last #{got[-1].inspect}")

if $fail > 0
  puts "shell term: #{$fail} of #{$count} checks failed"
  exit 1
end
puts "shell term: #{$count} checks passed (lines, colours, split reads, dropped sequences, \\r, \\b, tab, utf-8)"
