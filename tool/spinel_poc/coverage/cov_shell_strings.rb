# Coverage: shell_commands.rb-style string processing.
# split, command-line tokenizing, ljust/rjust formatting, strip, start_with?.

def parse_cmdline(line)
  line.strip.split(/\s+/)
end

lines = [
  "  ls -l /flash  ",
  "cat file.rb",
  "echo hello   world",
  "",
]

lines.each do |ln|
  toks = parse_cmdline(ln)
  cmd = toks[0] || "(none)"
  args = toks[1..-1] || []
  puts "cmd=#{cmd} argc=#{args.size} args=#{args.inspect}"
end

# tabular formatting like a shell `ls` / help table
rows = [
  ["help", "show commands"],
  ["ls", "list files"],
  ["cat", "print a file"],
]
rows.each do |name, desc|
  # to_s keeps a concrete String so ljust dispatches under Spinel (see
  # coverage/UNSUPPORTED.md U-1: poly.ljust is not yet dispatched)
  puts "#{name.to_s.ljust(8)}#{desc}"
end

# option detection
["--verbose", "-x", "plain", "--"].each do |a|
  kind = if a.start_with?("--")
           "long"
         elsif a.start_with?("-") && a.length > 1
           "short"
         else
           "arg"
         end
  puts "#{a} -> #{kind}"
end
