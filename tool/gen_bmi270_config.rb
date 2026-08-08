#!/usr/bin/env ruby
# Convert the BMI270 configuration array from a C source file into the raw
# 8192-byte binary that the Ruby driver uploads at init time.
#
# The BMI270 has no usable output until this blob is written into its internal
# memory (registers 0x5B/0x5C address, 0x5E data). Keeping it as a file in the
# filesystem instead of a Ruby literal keeps it out of the firmware image and
# lets the driver stream it in chunks.
#
# Usage:
#   ruby tool/gen_bmi270_config.rb [input.inl] [output.bin]
#
# Defaults read the array shipped with the M5Unified component and write it to
# the filesystem image. Provenance and license are recorded next to the output.

INPUT = ARGV[0] ||
        "managed_components/m5stack__m5unified/src/utility/imu/BMI270_config.inl"
OUTPUT = ARGV[1] || "flash/usr/share/imu/bmi270_config.bin"
EXPECTED_SIZE = 8192

unless File.exist?(INPUT)
  abort "input not found: #{INPUT}\n" \
        "(the M5Unified component is fetched during an esp32 build)"
end

text = File.read(INPUT)

# Take everything between the first '{' and the matching last '}' so that a
# leading declaration or a trailing comment cannot contribute stray numbers.
body = text[/\{(.*)\}/m, 1]
abort "no array body found in #{INPUT}" unless body

bytes = body.scan(/0x([0-9a-fA-F]{1,2})\b/).flatten.map { |h| h.to_i(16) }

if bytes.size != EXPECTED_SIZE
  abort "expected #{EXPECTED_SIZE} bytes, parsed #{bytes.size}"
end

require "fileutils"
FileUtils.mkdir_p(File.dirname(OUTPUT))
File.open(OUTPUT, "wb") { |f| f.write(bytes.pack("C*")) }

puts "wrote #{OUTPUT} (#{bytes.size} bytes)"
