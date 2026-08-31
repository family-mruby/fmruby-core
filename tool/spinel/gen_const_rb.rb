#!/usr/bin/env ruby
# Generate the FmrbConst Ruby module the Spinel kernel needs, from the C source
# of truth (headers + const.c), so the values never drift from the mruby build.
#
# The mruby build gets FmrbConst from picoruby-fmrb-const (const.c) at runtime.
# The Spinel build has no mruby C-extension layer, so we emit a plain Ruby module
# with the SAME values. Only the subset the kernel VM uses is generated (the
# desktop-only THEME_*/KEY_*/PROC_STATE_* live in the desktop port, Phase 4).
#
# Usage: ruby tool/spinel/gen_const_rb.rb <out.rb> [platform]
#   platform defaults to "linux".

require "fileutils"

ROOT = File.expand_path("../..", File.dirname(__FILE__))
out_path = ARGV[0] or abort "usage: gen_const_rb.rb <out.rb> [platform]"
platform = ARGV[1] || "linux"

def slurp(rel)
  path = File.join(ROOT, rel)
  File.exist?(path) ? File.read(path) : (abort "missing #{rel}")
end

# Parse a C enum body, honoring `NAME = value` and auto-increment. Returns a
# Hash{name => integer} for the requested names.
def enum_values(src, enum_marker, names)
  # find the enum block containing enum_marker
  idx = src.index(enum_marker) or abort "enum marker #{enum_marker} not found"
  open = src.rindex("{", idx) or abort "enum open brace not found for #{enum_marker}"
  close = src.index("}", open) or abort "enum close brace not found"
  body = src[open + 1...close]
  # Strip comments up front so a trailing `// ...` never spills the next
  # entry's name onto a second line within a single comma-separated piece.
  body = body.gsub(%r{/\*.*?\*/}m, "").gsub(%r{//[^\n]*}, "")
  vals = {}
  cur = 0
  body.split(",").each do |ent|
    ent = ent.sub(%r{/\*.*?\*/}m, "").sub(%r{//.*$}, "").strip
    next if ent.empty?
    if ent =~ /\A(\w+)\s*=\s*(-?\d+)\z/
      cur = Integer($2)
      vals[$1] = cur
    elsif ent =~ /\A(\w+)\z/
      vals[$1] = cur
    end
    cur += 1
  end
  names.each_with_object({}) { |n, h| h[n] = (vals[n] or abort "enum value #{n} not found") }
end

# Parse `#define NAME value` integers.
def define_values(src, names)
  names.each_with_object({}) do |n, h|
    if src =~ /^\s*#define\s+#{Regexp.escape(n)}\s+(\S+)/
      h[n] = Integer($1, 10) rescue Integer($1)
    else
      abort "#define #{n} not found"
    end
  end
end

# Parse `define_int_const(mrb, m, "NAME", 0xNN);` entries from const.c.
def int_const_values(src, names)
  names.each_with_object({}) do |n, h|
    if src =~ /define_int_const\([^,]+,[^,]+,\s*"#{Regexp.escape(n)}"\s*,\s*(0x[0-9A-Fa-f]+|-?\d+)\s*\)/
      h[n] = Integer($1)
    else
      abort "int const #{n} not found"
    end
  end
end

# Parse the `static fmrb_theme_t g_theme = { .field = 0xNN, ... };` initializer.
def theme_values(src, fields)
  idx = src.index("g_theme") or abort "g_theme not found"
  open = src.index("{", idx) or abort "g_theme brace not found"
  close = src.index("}", open) or abort "g_theme close not found"
  body = src[open + 1...close]
  fields.each_with_object({}) do |f, h|
    if body =~ /\.#{Regexp.escape(f)}\s*=\s*(0x[0-9A-Fa-f]+|\d+)/
      h[f] = Integer($1)
    else
      abort "theme field #{f} not found"
    end
  end
end

# Extract a string #define value (e.g. FMRB_OS_VERSION "1.0.0").
def str_define(src, name)
  if src =~ /^\s*#define\s+#{Regexp.escape(name)}\s+"([^"]*)"/
    $1
  else
    abort "string #define #{name} not found"
  end
end

msg = slurp("components/fmrb_msg/fmrb_msg.h")
task = slurp("components/fmrb_common/include/fmrb_task_config.h")
led = slurp("main/drivers/led_status/status_led.h")
app_h = slurp("components/fmrb_common/include/fmrb_app.h")
fmrb_h = slurp("components/fmrb_common/include/fmrb.h")
const_c = slurp("lib/add/picoruby-fmrb-const/ports/esp32/const.c")

msg_types = enum_values(msg, "FMRB_MSG_TYPE_APP_CONTROL",
                        %w[FMRB_MSG_TYPE_APP_CONTROL FMRB_MSG_TYPE_APP_GFX
                           FMRB_MSG_TYPE_APP_AUDIO FMRB_MSG_TYPE_HID_EVENT])
# PROC_ID_USER_APP0 is the lower bound of the user app id range: the kernel
# tests a kill target against it, so a target can never be the kernel, the
# host, the desktop or an overlay.
proc_ids = enum_values(task, "PROC_ID_KERNEL",
                       %w[PROC_ID_KERNEL PROC_ID_HOST PROC_ID_SYSTEM_APP
                          PROC_ID_USER_APP0])
led_vals = define_values(led, %w[FMRB_LED_STATUS_VERSION_MISMATCH])

# Phase 4 (desktop): the app-facing constant subset the desktop / mixins use.
proc_states = enum_values(app_h, "PROC_STATE_FREE",
                          %w[PROC_STATE_FREE PROC_STATE_INIT PROC_STATE_RUNNING
                             PROC_STATE_SUSPENDED PROC_STATE_STOPPING])
theme = theme_values(const_c, %w[desktop_bg menu_bg window_bg text text_light
                                 highlight border button dir_color])
# Every KEY_* the mruby module defines, not a hand-picked pair: a key the
# desktop starts testing for (KEY_F10 for the menu bar, KEY_TAB in the file
# manager) is a NameError at the first keystroke on Spinel, and generation
# does not catch it. Pulling the whole table keeps the two engines equal by
# construction.
key_names = const_c.scan(/define_int_const\([^,]+,[^,]+,\s*"(KEY_\w+)"/).flatten.uniq
keys = int_const_values(const_c, key_names)
os_version = str_define(fmrb_h, "FMRB_OS_VERSION")
ga_version = str_define(fmrb_h, "FMRB_GA_VERSION")
link_version = define_values(fmrb_h, %w[FMRB_LINK_VERSION])["FMRB_LINK_VERSION"]

# Which board this build is for. rake keeps it in the environment (the Rakefile
# loads .env into ENV before any task runs), and the simulator follows the
# target it stands in for, exactly as the C macros do.
hw_target = ENV["FMRB_HW_TARGET"].to_s
# Keep in step with MODERN_HW_TARGETS in the Rakefile.
hw_family = %w[TAB5 NARYAv4].include?(hw_target) ? "modern" : "retro"

# BOARD and CHIP_MODEL, mirroring const.c: BOARD from the FMRB_HW_* macros,
# CHIP_MODEL from what esp_chip_info() reports on that board. Both were fixed
# at the S3's values while Spinel only ran on Narya and the simulator; a P4
# build now exists and reads them (clock_setting picks the RTC part number
# from CHIP_MODEL), so they follow the target.
#
# Only the values Spinel can actually be built for are listed. ATOM is
# suspended and has never run Spinel, so it is not a case here -- it would
# fall to the Narya default, which is at least the right chip.
board, chip_model =
  case
  when platform == "linux"    then ["linux",    "linux"]
  when hw_target == "TAB5"    then ["tab5",     "ESP32-P4"]
  when hw_target == "NARYAv4" then ["naryav4",  "ESP32-P4"]
  else                             ["narya_v3", "ESP32-S3"]
  end

out = +"# AUTO-GENERATED by tool/spinel/gen_const_rb.rb -- do not edit.\n"
out << "# FmrbConst subset used by the Spinel kernel VM (values from C headers).\n"
out << "module FmrbConst\n"
out << "  PLATFORM = #{platform.inspect}\n"
out << "  BOARD = #{board.inspect}\n"
out << "  CHIP_MODEL = #{chip_model.inspect}\n"
# Which machine's habits this build has, the same answer const.c gives the
# mruby build from FMRB_HW_FAMILY_MODERN.
out << "  HW_FAMILY = #{hw_family.inspect}\n"
# WiFi-capable: every Spinel target has it (Narya S3 native, P4 via C6, the
# Linux sim reports host network state). ATOM (no WiFi) never runs Spinel.
out << "  HAS_WIFI = true\n"
out << "  MSG_TYPE_APP_CONTROL = #{msg_types['FMRB_MSG_TYPE_APP_CONTROL']}\n"
out << "  MSG_TYPE_APP_GFX = #{msg_types['FMRB_MSG_TYPE_APP_GFX']}\n"
out << "  MSG_TYPE_APP_AUDIO = #{msg_types['FMRB_MSG_TYPE_APP_AUDIO']}\n"
out << "  MSG_TYPE_HID_EVENT = #{msg_types['FMRB_MSG_TYPE_HID_EVENT']}\n"
out << "  PROC_ID_KERNEL = #{proc_ids['PROC_ID_KERNEL']}\n"
out << "  PROC_ID_HOST = #{proc_ids['PROC_ID_HOST']}\n"
out << "  PROC_ID_SYSTEM_APP = #{proc_ids['PROC_ID_SYSTEM_APP']}\n"
out << "  PROC_ID_USER_APP0 = #{proc_ids['PROC_ID_USER_APP0']}\n"
out << "  LED_ERR_VERSION_MISMATCH = #{led_vals['FMRB_LED_STATUS_VERSION_MISMATCH']}\n"
# ---- Phase 4 desktop subset ----
# HID event keycodes (USB HID Usage IDs) used by desktop key handling.
key_names.each { |k| out << "  #{k} = #{keys[k]}\n" }
# Process states (ps / taskbar).
out << "  PROC_STATE_FREE = #{proc_states['PROC_STATE_FREE']}\n"
out << "  PROC_STATE_INIT = #{proc_states['PROC_STATE_INIT']}\n"
out << "  PROC_STATE_RUNNING = #{proc_states['PROC_STATE_RUNNING']}\n"
out << "  PROC_STATE_SUSPENDED = #{proc_states['PROC_STATE_SUSPENDED']}\n"
out << "  PROC_STATE_STOPPING = #{proc_states['PROC_STATE_STOPPING']}\n"
# Theme colors: read from fmrb_theme_get() when the program starts (the kernel
# and app shims both export fmrb_spx_theme_color), so [theme] in
# system_conf.toml reaches Spinel programs the way it reaches mruby VMs. They
# used to be baked in here from const.c's defaults, which is why a theme edit
# changed the mruby desktop and not the Spinel one. The index is the field
# order of fmrb_theme_t (picoruby_fmrb_const.h); the parse of g_theme above is
# kept only to fail loudly if a field is renamed.
%w[desktop_bg menu_bg window_bg text text_light highlight border button dir_color].each_with_index do |f, i|
  abort "theme field #{f} vanished from const.c" unless theme.key?(f)
  out << "  THEME_#{f.upcase} = FmrbSpx.fmrb_spx_theme_color(#{i})\n"
end
# Rows one wheel notch scrolls, read at start-up for the same reason.
out << "  WHEEL_LINES = FmrbSpx.fmrb_spx_wheel_lines\n"

# Versions + platform/system placeholders (about dialog / config dialog). The
# Linux mruby build reports the same placeholders; ESP32 fills real values at
# runtime (not available at Spinel compile time -- acceptable for the port).
out << "  OS_VERSION = #{os_version.inspect}\n"
out << "  GA_VERSION = #{ga_version.inspect}\n"
out << "  LINK_VERSION = #{link_version}\n"
out << "  IDF_VERSION = \"-\"\n"
out << "  MAC_ADDRESS = \"-\"\n"
out << "  CHIP_REVISION = \"-\"\n"
out << "  CHIP_CORES = 0\n"
out << "  FLASH_SIZE_MB = 0\n"
out << "  PSRAM_SIZE_MB = 0\n"
out << "  RESET_REASON = \"-\"\n"
out << "  LANGUAGE = \"en\"\n"
out << "end\n"

FileUtils.mkdir_p(File.dirname(out_path))
File.write(out_path, out)
warn "Wrote #{out_path} (platform=#{platform})"
