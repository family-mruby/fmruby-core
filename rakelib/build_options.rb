# frozen_string_literal: true
#
# rakelib/build_options.rb - the build options an ESP32 build/ was made with,
# and the guard that refuses to go on with other ones (doc/api_fixes F9).
#
# The firmware is two builds glued together: libmruby, made by rake from the
# gembox and lib/upstream/family_mruby_esp32*.rb, and the app, made by CMake.
# Both read the same environment variables (the Rakefile, after .env and the
# board defaults), but CMake reads them only when it configures, which a
# changed variable does not cause, and libmruby is rebuilt only when its .a
# is missing. So a build/ made with one choice and built again with another
# mixes the two (a gem in without its port, the old engine linked, ...).
# rake build:esp32 records the options in build/fmrb_build_options.json and
# stops, saying what differs and what to run, when the next build asks for
# other ones; the flash tasks stop the same way before touching the port
# (idf.py flash builds again first, with the flash's own environment).
#
# Plain Ruby without rake, so the host test (test/build_options/run.rb) calls
# it directly. The sim, the native program and the browser build have their
# own stamp of the sources (tools/build_stamp.rb); this is the ESP32 build's
# options only.
require "fileutils"
require "json"

module FmrbBuildOptions
  FILE = "fmrb_build_options.json"
  # The F8 record of FMRB_EDITOR alone, read when there is no FILE yet and
  # removed once FILE is written.
  LEGACY_EDITOR_FILE = "fmrb_editor_option"
  OVERRIDE = "BUILD_OPTS_OK"
  MODERN_HW_TARGETS = %w[TAB5 NARYAv4].freeze

  # Every option that changes what goes into the firmware, in the order the
  # messages list them. The values are what the build does with the
  # environment (Rakefile, main/CMakeLists.txt, the gembox), not the raw text:
  # FMRB_FONT_JA unset and "1" are the same build.
  NAMES = %w[FMRB_HW_TARGET FMRB_KERNEL_ENGINE FMRB_APP_ENGINE_DESKTOP FMRB_APP_ENGINE_EDITOR
             FMRB_FFT_SPINEL FMRB_FONT_JA FMRB_ASTERISM FMRB_IMU FMRB_EDITOR
             FMRB_GC_PROFILE FMRB_TASK_SELF_TICK FMRB_DISPLAY_BACKEND DOCKER_IMAGE].freeze

  module_function

  def chip_of(hw_target) = MODERN_HW_TARGETS.include?(hw_target) ? "esp32p4" : "esp32s3"

  # The options this rake run asks for. env is ENV after the Rakefile has
  # laid .env and the board defaults on it; image the container (IMAGE).
  def current(env, image)
    hw = env["FMRB_HW_TARGET"].to_s.strip
    p4 = chip_of(hw) == "esp32p4"
    on = ->(k, default) { (env[k] || default) != "0" ? "1" : "0" }
    one = ->(k) { env[k] == "1" ? "1" : "0" }
    engine = ->(k) { v = env[k].to_s; v.empty? ? "mruby" : v }
    editor = on.("FMRB_EDITOR", "1")
    {
      "FMRB_HW_TARGET" => hw,
      "FMRB_KERNEL_ENGINE" => engine.("FMRB_KERNEL_ENGINE"),
      "FMRB_APP_ENGINE_DESKTOP" => engine.("FMRB_APP_ENGINE_DESKTOP"),
      # No editor, nothing for its engine to compile (main/CMakeLists.txt).
      "FMRB_APP_ENGINE_EDITOR" => editor == "0" ? "none" : engine.("FMRB_APP_ENGINE_EDITOR"),
      "FMRB_FFT_SPINEL" => on.("FMRB_FFT_SPINEL", "1"),
      "FMRB_FONT_JA" => on.("FMRB_FONT_JA", "1"),
      # The P4 build always has Asterism and the BMI270 driver; the two
      # variables choose them on the S3 only.
      "FMRB_ASTERISM" => p4 ? "1" : one.("FMRB_ASTERISM"),
      "FMRB_IMU" => p4 ? "1" : one.("FMRB_IMU"),
      "FMRB_EDITOR" => editor,
      "FMRB_GC_PROFILE" => one.("FMRB_GC_PROFILE"),
      "FMRB_TASK_SELF_TICK" => one.("FMRB_TASK_SELF_TICK"),
      # Only "cpu" changes anything (main/CMakeLists.txt).
      "FMRB_DISPLAY_BACKEND" => env["FMRB_DISPLAY_BACKEND"] == "cpu" ? "cpu" : "ppa",
      "DOCKER_IMAGE" => image.to_s,
    }
  end

  def configured?(build_dir) = File.exist?(File.join(build_dir, "CMakeCache.txt"))

  # The record of build_dir: [options, :record], or what can be told from a
  # build/ made before the record ([partial options, :inferred]), or
  # [nil, :none] for a build/ that is not configured.
  def recorded(build_dir, sdkconfig: "sdkconfig")
    return [nil, :none] unless configured?(build_dir)
    path = File.join(build_dir, FILE)
    if File.exist?(path)
      begin
        opts = JSON.parse(File.read(path))
        return [opts, :record] if opts.is_a?(Hash)
      rescue JSON::ParserError
        nil
      end
    end
    [infer(build_dir, sdkconfig), :inferred]
  end

  # What an unrecorded build/ says about itself: the HW target CMake keeps
  # (build:esp32 passes -DFMRB_HW_TARGET only for a named target, so none in
  # the cache is the unnamed one), checked against the chip of the sdkconfig,
  # and F8's FMRB_EDITOR record (a build/ older than that had the editor).
  # Anything else is unknown and not compared.
  def infer(build_dir, sdkconfig)
    out = {}
    cache = File.read(File.join(build_dir, "CMakeCache.txt"))
    hw = cache[/^FMRB_HW_TARGET:\w+=(.*)$/, 1].to_s.strip
    chip = File.exist?(sdkconfig) ? File.read(sdkconfig)[/^CONFIG_IDF_TARGET="(\w+)"/, 1] : nil
    chip ||= cache[/^IDF_TARGET:\w+=(.*)$/, 1]
    # A cache whose target and chip disagree tells nothing for sure.
    out["FMRB_HW_TARGET"] = hw if chip.nil? || chip == chip_of(hw)
    legacy = File.join(build_dir, LEGACY_EDITOR_FILE)
    out["FMRB_EDITOR"] = File.exist?(legacy) ? File.read(legacy).strip : "1"
    out
  end

  # [[name, was, now], ...] for the names both sides know and that differ.
  def differences(was, now)
    NAMES.filter_map do |k|
      next unless was.key?(k) && now.key?(k)
      [k, was[k], now[k]] unless was[k].to_s == now[k].to_s
    end
  end

  def show(value) = value.to_s.empty? ? "(none)" : value.to_s

  # The env assignments that ask for the recorded build again (those that
  # differ), for the message.
  def asking_for(diffs)
    diffs.map do |k, was, _|
      next "DOCKER_IMAGE=#{was}" if k == "DOCKER_IMAGE"
      next "FMRB_APP_ENGINE_EDITOR=<any> (no editor)" if was == "none"
      next "FMRB_DISPLAY_BACKEND=" if k == "FMRB_DISPLAY_BACKEND" && was == "ppa"
      "#{k}=#{was}"
    end.join(" ")
  end

  # The guard. Returns nil when the build may go on, else the message to stop
  # with. what is "build" or "flash" (the wording of the way out).
  def refusal(build_dir, now, what: "build", task: "build:esp32", sdkconfig: "sdkconfig",
              allow: false)
    was, kind = recorded(build_dir, sdkconfig: sdkconfig)
    return nil if was.nil?
    diffs = differences(was, now)
    return nil if diffs.empty? || allow
    w = diffs.map { |k, _, _| k.size }.max
    lines = diffs.map { |k, a, b| format("  %-#{w}s  %s (#{build_dir}/) -> %s (now)", k, show(a), show(b)) }
    from = kind == :inferred ? "#{build_dir}/ (as its CMake cache tells; it has no #{FILE})" : "#{build_dir}/"
    head = "#{task}: #{from} was made with other build options than this run asks for:"
    redo_ = "the options it was made with: #{asking_for(diffs)} rake <the same task>"
    body =
      if what == "flash"
        ["idf.py flash builds again first with this run's options, and #{build_dir}/ would be",
         "flashed as a mix of the two (or as the firmware of another board).",
         "Flash it with #{redo_},",
         "or build what you asked for first: `rake clean_all`, then `rake build:esp32` with these options."]
      else
        ["CMake reads them only when it configures (a changed variable does not make it",
         "configure again) and libmruby is rebuilt only when it is missing, so building on",
         "would mix the two.",
         "Start over: `rake clean_all` (it removes the ESP32 build only; the sim's build-linux/,",
         "the native program and the browser build stay), then build again.",
         "Or build with #{redo_}."]
      end
    ([head] + lines + body + ["#{OVERRIDE}=1 goes on anyway (when you know CMake will configure again)."]).join("\n")
  end

  # The record of a build that is about to start with now: written over the
  # old one (and over F8's FMRB_EDITOR file, which it replaces).
  def write(build_dir, now)
    FileUtils.mkdir_p(build_dir)
    File.write(File.join(build_dir, FILE), JSON.pretty_generate(now) + "\n")
    legacy = File.join(build_dir, LEGACY_EDITOR_FILE)
    File.delete(legacy) if File.exist?(legacy)
  end
end
