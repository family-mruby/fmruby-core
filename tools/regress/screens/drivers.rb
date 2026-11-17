# frozen_string_literal: true
#
# drivers.rb - the machines rake regress:screens drives, behind one face:
# input (fmrb_input.rb's words), capture (an Image), launch by path, the
# process list and closing what a scene opened, and at the end whether the
# machine is still there and its log has no crash in it.
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
require_relative "../tree"
require_relative "../fixtures"
require_relative "../boot_log"

module Regress
  module Screens
    # family-mruby, or FMRB_ROOT= (tree.rb)
    ROOT = Tree::ROOT
    TOOLS = Tree::TOOLS

    class DriverError < StandardError; end

    # What a crash leaves in the core's log: the boards' marks (boot_log.rb)
    # and what a Linux process prints when it dies (the sim's container and
    # the native program are such processes).
    CRASH = Regexp.union(BootLog::CRASH, /Segmentation fault/, /core dumped/, /AddressSanitizer/,
                         /terminate called/, /\bSIGSEGV\b/, /\bSIGABRT\b/).freeze

    def self.crash_lines(log)
      log.to_s.scrub.each_line.grep(CRASH).map(&:strip)
    end

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
        pid = dbg("spawn", "path=#{path}")["pid"]
        @spawned << pid
        pid
      end

      def apps
        dbg("ps").fetch("apps").to_h { |a| [a["pid"], a["name"]] }
      end

      def kill(pid)
        dbg("kill", "pid=#{pid}")
      end

      # The debug client's failures (a path that does not open: error -7)
      # are the scene's, not the run's.
      def dbg(*args)
        @sim.dbg(*args)
      rescue RuntimeError => e
        raise DriverError, e.message.strip
      end

      # The core's log since the container started, and whether the core is
      # still up and answering.
      def core_log = @sim.core_log
      def log_name = "the core's log (docker logs fmruby_core)"

      def alive?
        @sim.core_running? && !apps.empty?
      rescue DriverError
        false
      end

      # The apps of the desktop at rest, taken once after boot: anything
      # else at the end of a scene was opened by it.
      def settle_baseline!
        @resident = apps.keys
      end

      def close_extra_apps
        extra = apps.keys - @resident
        extra.each { |pid| kill(pid) }
        @spawned.clear
        extra.size
      end

      def mark_log!
        @log_mark = @sim.core_log.bytesize
      end

      # The sim's file system is this checkout's flash/ (the flat table of
      # fmrb_hal_file_posix.c), shared with whoever runs the sim later: what
      # a fixture replaces is put back by restore_fixtures!.
      FLASH = File.join(Tree::CORE, "flash")

      def seed_fm!
        @fm_state_was ||= begin
          f = File.join(FLASH, Fixtures::FM_STATE_VPATH)
          [File.exist?(f) ? File.binread(f) : nil]
        end
        Fixtures.write_fm_tree(FLASH)
        File.binwrite(File.join(FLASH, Fixtures::FM_STATE_VPATH), Fixtures.fm_state)
      end

      def restore_fixtures!
        return unless @fm_state_was
        FileUtils.rm_rf(File.join(FLASH, Fixtures::FM_DIR))
        f = File.join(FLASH, Fixtures::FM_STATE_VPATH)
        was = @fm_state_was.first
        was ? File.binwrite(f, was) : FileUtils.rm_f(f)
        @fm_state_was = nil
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

      # The page's launch slot (wasm/backend/launch_wasm.c, fmrb_web.rb
      # launch): the pid, or a DriverError when the firmware refused the path.
      def launch(path)
        JSON.parse(tool("launch", path).lines.last.to_s).fetch("pid")
      rescue JSON::ParserError, KeyError => e
        raise DriverError, "launch #{path}: #{e.message}"
      end

      # The launcher's fixed index, written by the page at every boot of this
      # tab (fmrb_web.rb seed); takes effect from the next reload or reset.
      def seed_launcher!
        tool("seed", Fixtures::LAUNCHER_INDEX, "/flash#{Fixtures::LAUNCHER_CACHE_VPATH}")
      end

      def seed_fm!
        raise DriverError, "the browser build has no File Manager fixture (no scene needs one)"
      end

      def restore_fixtures!; end

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

      # The page keeps no log a tool can read (the firmware prints to the
      # browser's console): what is left is that the page still answers with
      # the machine running.
      def core_log = nil
      def log_name = "the page"

      def alive?
        status.start_with?("running=true")
      rescue DriverError
        false
      end
    end
  
    class NativeDriver
      attr_reader :width, :height

      # Beside a developer's native program (/tmp/fmrb-native-<uid>, 5565),
      # and this tree's own (tree.rb): another tree's run goes on beside it.
      RUN_DIR = Tree.run_dir("fmrb-native-regress")
      DEBUG_PORT = Tree.port(5575, :screens_native)

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
        @cache_dir = cache_dir
        # the launcher's fixed index in the cache root (/var is the cache's)
        idx = File.join(cache_dir, Fixtures::LAUNCHER_CACHE_VPATH)
        FileUtils.mkdir_p(File.dirname(idx))
        File.binwrite(idx, Fixtures.launcher_index)
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
        dbg("spawn", path).fetch("pid")
      rescue KeyError
        raise DriverError, "app spawn #{path}: no pid in the answer"
      end

      def kill(pid)
        dbg("kill", pid.to_s)
      end

      def log_name = "the native program's log (#{log_path})"

      def alive?
        running? && !apps.empty?
      rescue DriverError
        false
      end

      def apps
        dbg("ps").fetch("apps").to_h { |a| [a["pid"], a["name"]] }
      end

      def settle_baseline!
        @resident = apps.keys
      end

      def close_extra_apps
        extra = apps.keys - @resident
        extra.each { |pid| kill(pid) }
        extra.size
      end

      def core_log
        File.exist?(log_path) ? File.binread(log_path) : ""
      end

      def log_path = File.join(@run_dir, "core.log")

      # /tmp is the cache root's (the layered table): a directory of this
      # run's own, emptied at every start, so nothing to put back.
      def seed_fm!
        raise DriverError, "seed_fm!: the native program was not started by this run" unless @cache_dir
        Fixtures.write_fm_tree(@cache_dir)
        File.binwrite(File.join(@cache_dir, Fixtures::FM_STATE_VPATH), Fixtures.fm_state)
      end

      def restore_fixtures!; end

      def mark_log!
        @log_mark = core_log.bytesize
      end

      def log_since_mark
        core_log.byteslice(@log_mark..) || ""
      end
    end
  end
end
