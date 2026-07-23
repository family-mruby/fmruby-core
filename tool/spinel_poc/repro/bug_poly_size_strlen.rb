def check(msg)
  d = msg[:data]
  puts "size=#{d.size}"
  puts "bytesize=#{d.bytesize}"
  return "early" if d.size < 6
  "ok"
end
s = "\x04\x01\x00\x00\x00\x00"
puts check({ data: s, src: "1" })
