# frozen_string_literal: true
#
# drivers.rb - the two machines rake regress:screens drives, behind one
# face: input (fmrb_input.rb's words), capture (an Image), launch by path,
# and closing what a scene opened.
#
#   SimDriver  the Linux sim the runner started (tools/fmrb_input.rb,
#              tools/fmrb_screenshot.py, the debug server's spawn/ps/kill)
#   WebDriver  the browser build in a headless browser the runner started on
#              its own development server (tools/fmrb_web.rb)
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
  end
end
