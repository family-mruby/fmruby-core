#!/usr/bin/env ruby
# Concatenate the Spinel kernel's Ruby sources into one combined file for
# `spinel --no-main --entry fmrb_kernel_entry`. Order matters: FFI + constants +
# base layer first, then the shared FmrbKernelImpl mixins and main file.
#
# Usage: ruby tool/spinel/gen_kernel_combined.rb <out.rb> [platform]

require "fileutils"

ROOT = File.expand_path("../..", File.dirname(__FILE__))
out_path = ARGV[0] or abort "usage: gen_kernel_combined.rb <out.rb> [platform]"
platform = ARGV[1] || "linux"

SPINEL_DIR = File.join(ROOT, "main/prebuild_scripts/spinel")
KERNEL_DIR = File.join(ROOT, "main/prebuild_scripts/kernel")
GEN_DIR = File.join(SPINEL_DIR, "gen")
FileUtils.mkdir_p(GEN_DIR)

# 1. Generate FmrbConst from the C source of truth.
const_rb = File.join(GEN_DIR, "fmrb_const_generated.rb")
system(RbConfig.ruby, File.join(ROOT, "tool/spinel/gen_const_rb.rb"), const_rb, platform) or
  abort "const generation failed"

# require_relative in fmrb_ffi.rb / base is avoided; we concatenate explicitly.
# The mixin files load in the same deterministic sort order picorbc uses
# (compile_ruby_to_bytecode.cmake sorts the subdir glob).
mixins = Dir.glob(File.join(KERNEL_DIR, "fmrb_kernel", "*.rb")).sort

parts = [
  File.join(SPINEL_DIR, "fmrb_ffi.rb"),
  const_rb,
  File.join(SPINEL_DIR, "msgpack_pure.rb"),
  File.join(SPINEL_DIR, "fmrb_kernel_base_spinel.rb"),
  *mixins,
  File.join(KERNEL_DIR, "fmrb_kernel.rb"),
]

combined = +"# AUTO-GENERATED combined Spinel kernel source -- do not edit.\n"
parts.each do |p|
  abort "missing part: #{p}" unless File.exist?(p)
  src = File.read(p)
  # Strip require_relative lines: everything is concatenated here.
  src = src.gsub(/^\s*require_relative\b.*$/, "# (require_relative stripped)")
  # Strip ESP32-only / mruby-only blocks marked for Spinel exclusion (e.g. the
  # RTC hardware access using picoruby I2C/RX8900 classes that do not exist in
  # the Spinel build). The block is unreachable on the Spinel Linux target.
  src = src.gsub(/^[ \t]*#:spinel-strip-begin\b.*?^[ \t]*#:spinel-strip-end\b.*?$/m,
                 "    # (spinel-strip block removed)")
  combined << "\n# ==== #{p.sub(ROOT + '/', '')} ====\n"
  combined << src
  combined << "\n"
end

FileUtils.mkdir_p(File.dirname(out_path))
# A constant the program names but the generated module lacks is not a
# generation error under Spinel: the reference compiles into a NameError that
# fires at the first use (KEY_F10 took the desktop down on its first keystroke,
# a month after the constant was added to const.c). Refuse to write a program
# that would do that.
defined_consts = File.read(const_rb).scan(/^  ([A-Z][A-Z0-9_]*) =/).flatten
code_lines = combined.lines.reject { |l| l.lstrip.start_with?("#") }
missing = code_lines.join.scan(/FmrbConst::([A-Z][A-Z0-9_]*)/).flatten.uniq - defined_consts
unless missing.empty?
  abort "FmrbConst constants used but not generated (tool/spinel/gen_const_rb.rb): #{missing.join(', ')}"
end
File.write(out_path, combined)
warn "Wrote #{out_path} (#{parts.size} parts, #{combined.lines.size} lines)"
