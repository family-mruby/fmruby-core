# Coverage: launcher.rb-style icon sort + grid layout math.
# Exercises sort {|a,b| ...}, integer division, [..].min, each_with_index.

ICON_W = 48
ICON_H = 40
COLS = 4

apps = [
  { name: "editor", order: 3 },
  { name: "shell", order: 1 },
  { name: "config", order: 5 },
  { name: "monitor", order: 2 },
  { name: "log_viewer", order: 4 },
]

# sort by order, then name (stable-ish, deterministic)
sorted = apps.sort { |a, b| a[:order] <=> b[:order] }

sorted.each_with_index do |app, i|
  col = i % COLS
  row = i / COLS
  x = 8 + col * (ICON_W + 8)
  y = 8 + row * (ICON_H + 16)
  puts "#{i}: #{app[:name]} order=#{app[:order]} at=(#{x},#{y}) col=#{col} row=#{row}"
end

# icon scale: fit a 12x10 glyph into (ICON_W-4)x(ICON_H-22)
iw = 12
ih = 10
scale = [(ICON_W - 4) / iw, (ICON_H - 22) / ih].min
puts "scale=#{scale}"
