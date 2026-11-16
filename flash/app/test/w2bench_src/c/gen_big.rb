# Generate a C wasm app with N small functions (the W2 probe of how the
# shared pool grows with the module's size). usage: ruby gen_big.rb N > big.c
n = Integer(ARGV[0] || 500)
puts '#include <stdint.h>'
puts '#include "fmrb_boundary.h"'
puts '#include "fmrb_sdk_version.h"'
puts 'FMRB_EVENT_BUFFER();'
puts 'FMRB_SDK_VERSION_SECTION();'
puts 'typedef int32_t (*fn_t)(int32_t);'
n.times do |i|
  puts "__attribute__((noinline)) static int32_t f#{i}(int32_t x) { " \
       "x = x * #{i + 3} + #{i * 7 + 1}; if (x & #{1 << (i % 16)}) x ^= #{i * 13 + 5}; " \
       "return (x >> 2) + #{i}; }"
end
puts "static const fn_t table[#{n}] = { #{(0...n).map { |i| "f#{i}" }.join(', ')} };"
puts 'static int32_t step;'
puts 'void on_create(void) { fmrb_log_info_raw("W2B big start", 13); }'
puts 'int32_t on_update(void) {'
puts "  int32_t v = step++; for (int32_t i = 0; i < #{n}; i++) v = table[i](v);"
puts '  fmrb_gfx_fill_rect(0, 0, 0, 8, 8, v & 0xFE); fmrb_gfx_present_raw(0, 0, 0, 0);'
puts '  return 500; }'
