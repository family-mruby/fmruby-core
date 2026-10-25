# frozen_string_literal: true
#
# stand_ins.rb - what the firmware's C gives an app, for host Ruby.
#
# Loaded in the forked child of one run (one scene on one profile), before
# the gem's Ruby and the app. Everything that would make a record differ
# from run to run is fixed here: the clock (Machine.board_millis, Time.now)
# moves only when the scene says, rand is seeded, ids count from 1, the
# network is a fake one the scene drives (fake_asterism.rb).
require "json"
require "set"

module Regress
  # The run's state: the target, the clock, the outbox of app messages.
  module Env
    class << self
      attr_accessor :target, :lang, :ms, :ids, :outbox, :app_path, :images, :wifi, :video, :unstubbed, :extra

      def reset(target, lang, app_path)
        @target = target
        @lang = lang
        @ms = 1_000_000
        @ids = 100
        @outbox = []
        @app_path = app_path
        @images = {}
        @wifi = { connected: true, ssid: "fmrb-net", ip: "192.0.2.10", hostname: "fmruby-a1b2c3", rssi: -50 }
        @video = nil
        @unstubbed = []
        @extra = {}
      end

      # [w, h] of an image file the app creates (a BMP's own header, else
      # what the scene said, else 32x32), nil when there is no such file.
      def image_size(path)
        return @images[path] if @images.key?(path)
        host = FakeFS.read_path(path)
        return nil unless host
        head = File.__regress_open(host, "rb") { |f| f.read(26) }.to_s
        if head.start_with?("BM") && head.bytesize >= 26
          w, h = head.byteslice(18, 8).unpack("l<l<")
          return [w, h.abs]
        end
        if head.start_with?("\x89PNG".b)
          w, h = File.__regress_open(host, "rb") { |f| f.read(24) }.byteslice(16, 8).unpack("NN")
          return [w, h]
        end
        [32, 32]
      end

      def video_info = @video
      def video_status = @video ? { state: 1, shown: 0, dropped: 0 } : nil

      def note_unstubbed(name)
        @unstubbed << name unless @unstubbed.include?(name)
        nil
      end
    end
  end

  # FmrbConst for the target (const.c): keys, theme from the target's config.
  def self.define_const(target, lang, theme)
    m = Module.new
    keys = {
      "KEY_ENTER" => 0x28, "KEY_ESC" => 0x29, "KEY_BACKSPACE" => 0x2A, "KEY_TAB" => 0x2B, "KEY_SPACE" => 0x2C,
      "KEY_MINUS" => 0x2D, "KEY_EQUAL" => 0x2E, "KEY_LBRACKET" => 0x2F, "KEY_RBRACKET" => 0x30,
      "KEY_BACKSLASH" => 0x31, "KEY_SEMICOLON" => 0x33, "KEY_QUOTE" => 0x34, "KEY_GRAVE" => 0x35,
      "KEY_COMMA" => 0x36, "KEY_PERIOD" => 0x37, "KEY_SLASH" => 0x38, "KEY_CAPSLOCK" => 0x39,
      "KEY_PRINTSCREEN" => 0x46, "KEY_SCROLLLOCK" => 0x47, "KEY_PAUSE" => 0x48, "KEY_INSERT" => 0x49,
      "KEY_HOME" => 0x4A, "KEY_PGUP" => 0x4B, "KEY_DELETE" => 0x4C, "KEY_END" => 0x4D, "KEY_PGDN" => 0x4E,
      "KEY_RIGHT" => 0x4F, "KEY_LEFT" => 0x50, "KEY_DOWN" => 0x51, "KEY_UP" => 0x52, "KEY_NUMLOCK" => 0x53,
      "KEY_LCTRL" => 0xE0, "KEY_LSHIFT" => 0xE1, "KEY_LALT" => 0xE2, "KEY_LMETA" => 0xE3,
      "KEY_RCTRL" => 0xE4, "KEY_RSHIFT" => 0xE5, "KEY_RALT" => 0xE6, "KEY_RMETA" => 0xE7,
      "MOD_LSHIFT" => 0x01, "MOD_RSHIFT" => 0x02, "MOD_LCTRL" => 0x04, "MOD_RCTRL" => 0x08,
      "MOD_LALT" => 0x10, "MOD_RALT" => 0x20, "MOD_SHIFT" => 0x03, "MOD_CTRL" => 0x0C, "MOD_ALT" => 0x30
    }
    ("A".."Z").each_with_index { |c, i| keys["KEY_#{c}"] = 0x04 + i }
    (1..9).each { |i| keys["KEY_#{i}"] = 0x1D + i }
    keys["KEY_0"] = 0x27
    (1..12).each { |i| keys["KEY_F#{i}"] = 0x39 + i }
    %w[SQUARE CROSS CIRCLE TRIANGLE L1 R1 L2 R2 SELECT START L3 R3 UP DOWN LEFT RIGHT].each_with_index { |n, i| keys["GP_#{n}"] = i }
    %w[LX LY RX RY].each_with_index { |n, i| keys["GP_AXIS_#{n}"] = i }
    keys.each { |k, v| m.const_set(k, v) }
    {
      "PLATFORM" => "esp32", "BOARD" => target.name, "HW_FAMILY" => target.family, "FONT_JA" => target.font_ja,
      "PROC_ID_KERNEL" => 0, "PROC_ID_HOST" => 1, "PROC_ID_SYSTEM_APP" => 2, "PROC_ID_USER_APP0" => 3,
      "PROC_ID_USER_APP1" => 4, "PROC_ID_USER_APP2" => 5, "PROC_ID_USER_APP3" => 6, "PROC_ID_USER_APP4" => 7,
      "PROC_STATE_FREE" => 0, "PROC_STATE_INIT" => 1, "PROC_STATE_RUNNING" => 2, "PROC_STATE_SUSPENDED" => 3,
      "PROC_STATE_STOPPING" => 4, "MSG_TYPE_APP_CONTROL" => 1, "MSG_TYPE_APP_GFX" => 2, "MSG_TYPE_APP_AUDIO" => 3,
      "MSG_TYPE_HID_EVENT" => 4, "APP_CTRL_SPAWN" => 0, "APP_CTRL_KILL" => 1, "APP_CTRL_SUSPEND" => 2,
      "APP_CTRL_RESUME" => 3, "MAX_PATH_LEN" => 256, "OS_VERSION" => "9.9.9", "GA_VERSION" => "9.9.9",
      "LINK_VERSION" => 1, "HAS_WIFI" => true, "BUILD_DATE" => "Oct 25 2026 12:00:00", "IDF_VERSION" => "v5.5.4",
      "MAC_ADDRESS" => "02:00:00:A1:B2:C3",
      "CHIP_MODEL" => target.family == "modern" ? "ESP32-P4" : "ESP32-S3", "CHIP_REVISION" => "v1.0",
      "CHIP_CORES" => 2, "FLASH_SIZE_MB" => 16, "PSRAM_SIZE_MB" => 8, "RESET_REASON" => "POWERON",
      "LED_ERR_NONE" => 0, "LED_ERR_FATAL" => 1, "LED_ERR_VERSION_MISMATCH" => 2, "LANGUAGE" => lang,
      "WHEEL_LINES" => 3
    }.each { |k, v| m.const_set(k, v) }
    %w[desktop_bg menu_bg window_bg text text_light highlight border button dir_color].each do |k|
      m.const_set("THEME_#{k.upcase}", theme[k])
    end
    m.define_singleton_method(:bt_mac) { "02:00:00:A1:B2:C4" }
    m.define_singleton_method(:const_missing) { |n| raise NameError, "FmrbConst::#{n} is not in the stand-in (tools/regress/stand_ins.rb)" }
    Object.const_set(:FmrbConst, m)
  end
end

module Machine
  def self.board_millis = Regress::Env.ms
  def self.delay_ms(ms) = (Regress::Env.ms += ms.to_i; nil)
  def self.sleep_ms(ms) = delay_ms(ms)
  def self.uptime_us = Regress::Env.ms * 1000
  def self.method_missing(n, *) = Regress::Env.note_unstubbed("Machine.#{n}")
  def self.respond_to_missing?(*) = true
end

module Kernel
  def sleep_ms(ms) = Machine.delay_ms(ms)
end

module Log
  LEVELS = %i[debug info warn error].freeze
  class << self
    LEVELS.each { |l| define_method(l) { |*_| nil } }
    # The log buffer (Log.read_lines(max, pos) -> [lines, new pos]): the
    # scene's lines (Env.extra[:log]).
    def read_lines(max, pos)
      all = Regress::Env.extra[:log] || []
      pos = pos.to_i
      [all[pos, max.to_i] || [], [all.size, pos + max.to_i].min]
    end

    def write_pos = (Regress::Env.extra[:log] || []).size
    def set_buffer_level(*) = nil
    def method_missing(n, *) = Regress::Env.note_unstubbed("Log.#{n}")
    def respond_to_missing?(*) = true
  end
end

module Task
  def self.pass = nil
  def self.method_missing(*) = nil
end

module MessagePack
  def self.pack(v) = v
  def self.unpack(v) = v
end

# Time on the fake clock: 2026-10-25 12:00:00 at the scene's start.
class Time
  class << self
    alias_method :__regress_now, :now
    def now = Time.at(1_792_900_800 + (Regress::Env.ms - 1_000_000) / 1000)
  end
end

class FmrbApp
  @@pid = 3

  def self.pid = @@pid
  # The tasks (the kernel's table): a scene may give its own (Env.extra[:ps]).
  def self.ps
    Regress::Env.extra[:ps] || [
      { id: 0, pid: 0, name: "kernel", state: 2, type: 0, mem_used: 0, mem_total: 0 },
      { id: 2, pid: 2, name: "desktop", state: 2, type: 1, mem_used: 180 * 1024, mem_total: 512 * 1024 },
      { id: @@pid, pid: @@pid, name: "app", state: 2, type: 2, mem_used: 96 * 1024, mem_total: 1024 * 1024 },
      { id: 4, pid: 4, name: "a_rather_long_application_name", state: 3, type: 2, mem_used: 700 * 1024,
        mem_total: 1024 * 1024 }
    ]
  end
  def self.ps_gen = 1
  def self.heap_info = { internal_free: 120_000, internal_total: 300_000, psram_free: 4_000_000, psram_total: 8_000_000,
                         internal_largest: 60_000, iram_free: 90_000, iram_total: 300_000, psram_largest: 2_000_000, used: 100_000, total: 400_000, free: 300_000 }
  def self.pool_usage = 25
  def self.pool_used = 256 * 1024
  def self.sys_pool_info = { used: 100_000, total: 400_000 }
  def self._get_last_error = nil
  def self.config(_k) = nil
  def self.uptime_us = Regress::Env.ms * 1000
  def self.script_path = Regress::Env.app_path
  def self.wallclock = [2026, 10, 25, 12, ((Regress::Env.ms - 1_000_000) / 60_000) % 60, ((Regress::Env.ms - 1_000_000) / 1000) % 60]
  def self.set_wallclock(*) = true
  def self.gfx_stats = { frames: 100, cmds: 1000, bytes: 100_000, dropped: 0 }
  def self.enable_cursor = nil
  def self.set_cursor_visible(*) = nil
  def self.reboot = nil
  def self.ble_start = nil
  def self.ble_state = 0
  def self.ble_settings_window(*) = nil
  def self.ble_settings_left = nil
  def self.set_kana_mode(*) = nil
  def self.audio_muted? = false
  def self.wifi_info = Regress::Env.wifi
  def self.method_missing(n, *) = Regress::Env.note_unstubbed("FmrbApp.#{n}")
  def self.respond_to_missing?(*) = true

  # What app.c's _init sets (the spawner's window, from the app's .app.toml).
  def _init
    w = Regress::Env.extra[:window]
    @_name = w[:name]
    @_fullscreen = w[:fullscreen]
    @_rounded_corners = w[:rounded]
    @platform = FmrbConst::PLATFORM
    @window_width = w[:w]
    @window_height = w[:h]
    @pos_x = w[:x]
    @pos_y = w[:y]
    @_canvas = w[:headless] ? nil : 1
    @_bg_canvas = nil
  end

  def _spin(_ms) = nil
  def _cleanup = nil
  def _create_canvas(_w, _h, _z, _t, _tc) = (Regress::Env.ids += 1)
  def _delete_canvas(_id) = nil
  def _set_window_param(k, v) = instance_variable_set(:"@#{k}", v)
  def _is_file_app = true
  def _mark_expected_stop = nil
  def _sync_file(*) = true
  def _send_audio_note(*) = nil
  def _audio_note_bytes(*) = nil

  def _send_message(dest, type, data)
    Regress::Env.outbox << [dest, type, data]
    nil
  end
end

# The firmware's mic, a stand-in that hears silence.
module FmrbMic
  def self.available? = false
  def self.rate = 16_000
  def self.enable(*) = false
  def self.read(*) = nil
end

# require of a board path ("/app/demo/picoruby_status") reads the file from
# the fake file system once, as the firmware does; a library CRuby does not
# have (a picoruby gem) is false, as an already-loaded one would be.
module Kernel
  alias_method :__regress_require, :require

  private def require(path)
    p = path.to_s
    if p.start_with?("/") && Regress::FakeFS.device?(p)
      p += ".rb" unless p.end_with?(".rb")
      $__regress_loaded ||= {}
      return false if $__regress_loaded[p]
      host = Regress::FakeFS.read_path(p) || raise(LoadError, "cannot load such file -- #{p}")
      $__regress_loaded[p] = true
      eval(File.__regress_read(host), TOPLEVEL_BINDING, host) # rubocop:disable Security/Eval
      return true
    end
    begin
      __regress_require(p)
    rescue LoadError
      false
    end
  end
end

# FmrbNet.request (fmrb-net.rb): answered at once from the scene's table
# (Env.extra[:http] = { url => body }); a URL not in it is a 404.
module FmrbNet
  class Request
    attr_reader :url, :status, :body, :error

    def initialize(url)
      @url = url
      body = (Regress::Env.extra[:http] || {})[url]
      @status = body ? 200 : 404
      @body = body
      @error = nil
    end

    def done? = true
    def ok? = @status == 200
    def cancel = nil
    def release = nil
  end

  def self.request(url) = Request.new(url)
end
