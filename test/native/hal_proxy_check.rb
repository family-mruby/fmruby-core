# Every function of the ESP32 file HAL that writes or erases the flash hands
# the work to hw_proxy when its caller's stack is in PSRAM: with the cache off
# for a flash write (no CONFIG_SPI_FLASH_AUTO_SUSPEND, as on the Tab5) such a
# stack cannot be touched, and the task panics. rename and remove once called
# the VFS directly and the Wi-Fi manager (PSRAM stack) fell over saving
# /home/wifi.toml.
#
# Reads the source: a public fmrb_hal_file_* function whose body makes one of
# the calls below must also ask hw_proxy_needs_proxy().
#
# Usage: ruby test/native/hal_proxy_check.rb [ROOT]

root = ARGV[0] || File.expand_path("../..", __dir__)
src = File.join(root, "components/fmrb_hal/platform/esp32/fmrb_hal_file_esp32.c")
text = File.read(src)

WRITES = /\b(unlink|rename|mkdir|rmdir|utime|chmod|fflush|fwrite|fopen|ftruncate|truncate|
           esp_littlefs_format|esp_partition_erase_range|esp_partition_write)\s*\(/x

# Run before any app or PSRAM-stack task exists (boot, from main's stack).
EXEMPT = %w[fmrb_hal_file_init fmrb_hal_file_deinit].freeze

checked = 0
missing = []
text.scan(/^[a-z_][\w \*]*?\b(fmrb_hal_file_\w+)\([^;{]*\)\s*\{(.*?)^\}/m) do |name, body|
  next if EXEMPT.include?(name)
  code = body.gsub(%r{//[^\n]*}, "")
  next unless code =~ WRITES
  checked += 1
  missing << "#{name} (#{Regexp.last_match(1)})" unless code.include?("hw_proxy_needs_proxy()")
end

if missing.empty? && checked.positive?
  puts "hal proxy check: #{checked} flash-writing functions, all route PSRAM-stack callers to hw_proxy"
  exit 0
end
warn "hal proxy check: no flash-writing function found in #{src}" if checked.zero?
missing.each { |m| warn "hal proxy check: #{m} writes the flash without asking hw_proxy_needs_proxy()" }
exit 1
