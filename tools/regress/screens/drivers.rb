# frozen_string_literal: true
#
# drivers.rb - the machines rake regress:screens drives, behind one face:
# input (fmrb_input.rb's words), capture (an Image), launch by path, and
# closing what a scene opened.
#
#   SimDriver     the Linux sim the runner started (tools/fmrb_input.rb,
#                 tools/fmrb_screenshot.py, the debug server's spawn/ps/kill)
#   WebDriver     the browser build in a headless browser the runner started
#                 on its own development server (tools/fmrb_web.rb)
#   NativeDriver  the native Linux program, headless (tools/fmrb_native.rb),
#                 in a run directory, debug port and user directory of its
#                 own, so a developer's running copy is not touched
#
# Both shell out to the root tools, which stay the one implementation of
# each protocol.
require "fileutils"
require "json"
require "open3"
require_relative "image"

module Regress
  module Screens
    ROOT = File.expand_path("../../../..", __dir__) # family-mruby
    TOOLS = File.join(ROOT, "tools")

    class DriverError < StandardError; end

    class SimDriver
      attr_reader :width, :height

      def initialize(sim, tmp)
        @sim = sim # Regress::SimRun (the bring-up and the debug client)
        @tmp = tmp
        @png = File.join(tmp, "capture.png")
        @spawned = []
        @log_mark = 0
      end

      def input(words)
        out, st = Open3.capture2e(Gem.ruby, File.join(TOOLS, "fmrb_input.rb"), *words, chdir: ROOT)
        raise DriverError, "fmrb_input.rb #{words.join(' ')}: #{out.strip}" unless st.success?
      end

      def capture
        FileUtils.rm_f(@png)
        out, st = Open3.capture2e("python3", File.join(TOOLS, "fmrb_screenshot.py"), @png, chdir: ROOT)
        raise DriverError, "fmrb_screenshot.py: #{out.strip}" unless st.success? && File.exist?(@png)
        img = Image.load(@png)
        @width = img.w
        @height = img.h
        img
      end

      def launch(path)
        @spawned << @sim.dbg("spawn", "path=#{path}")["pid"]
      end

      def apps
        @sim.dbg("ps").fetch("apps").to_h { |a| [a["pid"], a["name"]] }
      end

      # The apps of the desktop at rest, taken once after boot: anything
      # else at the end of a scene was opened by it.
      def settle_baseline!
        @resident = apps.keys
      end

      def close_extra_apps
        extra = apps.keys - @resident
        extra.each { |pid| @sim.dbg("kill", "pid=#{pid}") }
        @spawned.clear
        extra.size
      end

      def mark_log!
        @log_mark = @sim.core_log.bytesize
      end

      def log_since_mark
        @sim.core_log.byteslice(@log_mark..) || ""
      end
    end

    class WebDriver
      attr_reader :width, :height

      def initialize(port, tmp)
        @port = port
        @tmp = tmp
        @png = File.join(tmp, "capture.png")
      end

      def tool(*args, timeout: 30)
        out, st = Open3.capture2e(Gem.ruby, File.join(TOOLS, "fmrb_web.rb"), "--port", @port.to_s,
                                  "--timeout", timeout.to_s, *args, chdir: ROOT)
        raise DriverError, "fmrb_web.rb #{args.join(' ')}: #{out.strip.lines.last(3).join}" unless st.success?
        out
      end

      def input(words)
        tool(*words)
      end

      def capture
        FileUtils.rm_f(@png)
        tool("screenshot", @png)
        img = Image.load(@png)
        @width = img.w
        @height = img.h
        img
      end

      def launch(_path)
        raise DriverError, "the browser build has no launch by path"
      end

      def status
        tool("status")[/running=.*/].to_s
      end

      def settle_baseline!; end

      # No process list in the page: a scene that opened something closes
      # it itself, or the next scene reloads (close_extra_apps is the
      # runner's reload).
      def close_extra_apps = 0

      def mark_log!; end
      def log_since_mark = ""
    end
  
    class NativeDriver
      attr_reader :width, :height

      # Beside a developer's native program (/tmp/fmrb-native-<uid>, 5565).
      RUN_DIR = "/tmp/fmrb-native-regress-#{Process.uid}".freeze
      DEBUG_PORT = 5575

      def initialize(tmp, run_dir: RUN_DIR, port: DEBUG_PORT)
        @tmp = tmp
        @png = File.join(tmp, "capture.png")
        @run_dir = run_dir
        @port = port
        @log_mark = 0
      end

      def tool(*args, timeout: 60)
        env = { "FMRB_DEBUG_TCP_PORT" => @port.to_s }
        cmd = [Gem.ruby, File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run_dir, *args]
        out = +""
        st = nil
        Open3.popen2e(env, *cmd, chdir: ROOT) do |_in, io, wait|
          reader = Thread.new { io.read }
          unless wait.join(timeout)
            Process.kill("KILL", wait.pid) rescue nil # rubocop:disable Style/RescueModifier
            raise DriverError, "fmrb_native.rb #{args.first}: no answer in #{timeout}s"
          end
          out = reader.value
          st = wait.value
        end
        raise DriverError, "fmrb_native.rb #{args.join(' ')}: #{out.strip.lines.last(3).join}" unless st.success?
        out
      end

      def up(user_dir, cache_dir, machine: "sdl")
        tool("up", "--machine", machine, "--user-dir", user_dir, "--cache-dir", cache_dir,
             "--timeout", "90", timeout: 120)
      end

      def down
        tool("down", timeout: 30)
      end

      def running?
        tool("status", timeout: 15).include?("running,")
      rescue DriverError
        false
      end

      def input(words)
        tool("input", *words)
      end

      def capture
        FileUtils.rm_f(@png)
        tool("shot", @png)
        img = Image.load(@png)
        @width = img.w
        @height = img.h
        img
      end

      def dbg(*args)
        doc = JSON.parse(tool("app", *args).lines.last.to_s)
        raise DriverError, "app #{args.join(' ')}: #{doc['error']}" if doc["error"]
        doc
      rescue JSON::ParserError => e
        raise DriverError, "app #{args.join(' ')}: #{e.message}"
      end

      def launch(path)
        dbg("spawn", path)
      end

      def apps
        dbg("ps").fetch("apps").to_h { |a| [a["pid"], a["name"]] }
      end

      def settle_baseline!
        @resident = apps.keys
      end

      def close_extra_apps
        extra = apps.keys - @resident
        extra.each { |pid| dbg("kill", pid.to_s) }
        extra.size
      end

      def core_log
        File.exist?(log_path) ? File.binread(log_path) : ""
      end

      def log_path = File.join(@run_dir, "core.log")

      def mark_log!
        @log_mark = core_log.bytesize
      end

      def log_since_mark
        core_log.byteslice(@log_mark..) || ""
      end
    end
  end
end
