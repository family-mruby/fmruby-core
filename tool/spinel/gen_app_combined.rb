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
      File.join(SPINEL_DIR, "fmrb_editor_ffi.rb"),   # FmrbSpxEc + EditorCore
      File.join(SPINEL_DIR, "editor_debug_stub.rb"), # module EditorDebugPane (no-op)
      # The editor's own string table. The mruby build picks this up from the
      # editor/ directory glob; here every part is listed by hand.
      File.join(ROOT, "main/prebuild_scripts/default_app/editor/i18n.rb"),
    ],
    mixin_dir: nil,
    main: File.join(ROOT, "main/prebuild_scripts/default_app/editor.app.rb"),
  },
}

spec = APPS[app] or abort "unknown app: #{app} (known: #{APPS.keys.join(', ')})"

mixins = spec[:mixin_dir] ? Dir.glob(File.join(spec[:mixin_dir], "*.rb")).sort : []

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
File.write(out_path, combined)
warn "Wrote #{out_path} (#{parts.size} parts, #{combined.lines.size} lines)"
