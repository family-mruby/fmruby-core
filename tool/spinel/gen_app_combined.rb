#!/usr/bin/env ruby
# Concatenate a Spinel application's Ruby sources into one combined file for
# `spinel --no-main --entry <app>_entry`. Mirrors gen_kernel_combined.rb but for
# the application layer (FmrbApp / FmrbGfx base + the app's mixins + main file).
#
# The app's bare top-level start trailer (`app = SomeApp.new; app.start`) becomes
# the body of the named entry (Spinel wraps all top-level statements into the
# --entry function), so no explicit `def <app>_entry` wrapper is required --
# same convention the kernel uses.
#
# Usage: ruby tool/spinel/gen_app_combined.rb <app> <out.rb> [platform]
#   <app> currently supports: system_desktop, editor
#
# Order matters: FFI + constants + base layer, then the shared app libraries
# (FmrbI18n / sprite / audio), then the app's mixins (sorted like the picorbc
# glob), then the app main file (which includes the mixins and starts).

require "fileutils"

ROOT = File.expand_path("../..", File.dirname(__FILE__))
app = ARGV[0] or abort "usage: gen_app_combined.rb <app> <out.rb> [platform]"
out_path = ARGV[1] or abort "usage: gen_app_combined.rb <app> <out.rb> [platform]"
platform = ARGV[2] || "linux"

SPINEL_DIR = File.join(ROOT, "main/prebuild_scripts/spinel")
KERNEL_DIR = File.join(ROOT, "main/prebuild_scripts/kernel")
MRBLIB_DIR = File.join(ROOT, "lib/add/picoruby-fmrb-app/mrblib")
GEN_DIR = File.join(SPINEL_DIR, "gen")
FileUtils.mkdir_p(GEN_DIR)

# 1. Generate FmrbConst (superset incl. the desktop THEME_/KEY_/PROC_STATE_ set).
const_rb = File.join(GEN_DIR, "fmrb_const_generated.rb")
unless File.exist?(const_rb)
  system(RbConfig.ruby, File.join(ROOT, "tool/spinel/gen_const_rb.rb"), const_rb, platform) or
    abort "const generation failed"
end

# Per-app source manifest. Extend this hash to add more Spinel apps.
APPS = {
  "system_desktop" => {
    # Shared app libraries the desktop pulls in (must precede the mixins).
    libs: [
      File.join(MRBLIB_DIR, "fmrb-i18n.rb"),
      File.join(MRBLIB_DIR, "fmrb-sprite.rb"),
      File.join(MRBLIB_DIR, "fmrb-audio.rb"),
      File.join(MRBLIB_DIR, "fmrb-ui.rb"),
    ],
    # Mixin directory (globbed + sorted, like compile_ruby_to_bytecode.cmake).
    mixin_dir: File.join(KERNEL_DIR, "system_desktop"),
    main: File.join(KERNEL_DIR, "system_desktop.app.rb"),
  },
  # The editor. Its debugger UI (default_app/editor/debug_pane.rb) is replaced by
  # the no-op editor_debug_stub.rb: FMRB::Debug has no Spinel binding, and v1 of
  # the Spinel editor deliberately ships without the debugger (use the mruby
  # build for that). Everything else is the same source as the mruby build.
  "editor" => {
    libs: [
      File.join(MRBLIB_DIR, "fmrb-i18n.rb"),         # FmrbI18n (module only)
      File.join(MRBLIB_DIR, "fmrb-ui.rb"),           # FmrbUI widget set
      File.join(SPINEL_DIR, "fmrb_editor_ffi.rb"),   # FmrbSpxEc + EditorCore
      File.join(SPINEL_DIR, "editor_debug_stub.rb"), # module EditorDebugPane (no-op)
    ],
    # The editor's mixins (i18n string table, ti UI, ...) are globbed from the
    # editor/ directory, exactly like the mruby build's CMake glob, so a new
    # editor/*.rb file is picked up by both builds automatically. debug_pane.rb
    # is the one exception: the Spinel build has no FMRB::Debug binding, so the
    # no-op editor_debug_stub.rb (in libs above) stands in for it.
    mixin_dir: File.join(ROOT, "main/prebuild_scripts/default_app/editor"),
    mixin_exclude: ["debug_pane.rb"],
    main: File.join(ROOT, "main/prebuild_scripts/default_app/editor.app.rb"),
  },
}

spec = APPS[app] or abort "unknown app: #{app} (known: #{APPS.keys.join(', ')})"

mixins = spec[:mixin_dir] ? Dir.glob(File.join(spec[:mixin_dir], "*.rb")).sort : []
if spec[:mixin_exclude]
  mixins = mixins.reject { |m| spec[:mixin_exclude].include?(File.basename(m)) }
end

parts = [
  # NB: fmrb_ffi.rb (the full kernel FmrbSpx) is intentionally NOT included --
  # fmrb_app_ffi.rb declares the minimal FmrbSpx (millis + log) the app needs.
  # Splicing the kernel shim would emit externs for every kernel ffi_func and
  # break a mixed mruby-kernel + Spinel-desktop link.
  File.join(SPINEL_DIR, "fmrb_app_ffi.rb"),    # FmrbSpx(min) / FmrbSpxApp / FmrbSpxGfx
  const_rb,                                    # FmrbConst
  File.join(SPINEL_DIR, "msgpack_pure.rb"),    # MessagePack
  File.join(SPINEL_DIR, "fmrb_app_base_spinel.rb"), # Log/Machine/SpxBytes/FmrbGfx/GfxBlock/FmrbApp
  *spec[:libs],
  *mixins,
  spec[:main],
]

combined = +"# AUTO-GENERATED combined Spinel app source (#{app}) -- do not edit.\n"
parts.each do |p|
  abort "missing part: #{p}" unless File.exist?(p)
  src = File.read(p)
  # Everything is concatenated here; drop require_relative / require of siblings.
  src = src.gsub(/^\s*require_relative\b.*$/, "# (require_relative stripped)")
  # Strip blocks marked for Spinel exclusion (mruby/ESP32-only paths).
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
# Comments are skipped, trailing ones included: "FmrbConst::THEME_*" in prose
# is not a reference. (A "#" inside a string literal would also cut the line,
# which is harmless here: a constant reference never follows one.)
code_lines = combined.lines.map { |l| l.sub(/#.*$/, "") }
missing = code_lines.join.scan(/FmrbConst::([A-Z][A-Z0-9_]*)/).flatten.uniq - defined_consts
unless missing.empty?
  abort "FmrbConst constants used but not generated (tool/spinel/gen_const_rb.rb): #{missing.join(', ')}"
end
File.write(out_path, combined)
warn "Wrote #{out_path} (#{parts.size} parts, #{combined.lines.size} lines)"
