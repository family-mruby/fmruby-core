# frozen_string_literal: true
#
# dsl.rb - how a screen scene is written (test/regress/screens/*.rb,
# doc/regression/usage.md chapter 9).
#
#   Regress::Screens.scene "editor_type", on: %w[modern retro web] do |s|
#     s.open_editor                 # helpers from test/regress/screens/helpers.rb
#     s.mark
#     s.key "a"
#     s.wait_change s.editor_text_rect, what: "the typed key in the editor"
#   end
#
# A scene runs on every configuration it names (on:), each time from the
# desktop with no app open (the runner closes what a scene opened). What a
# scene looks at is that things come up and move, not what they look like
# (the draw records of rake regress cover that): the screen is not blank,
# a region changes after an input, an app is in the process list, the log
# says something. Waiting is on the screen, not on the clock. A scene passes
# when its block ends without a Failure and it confirmed at least one thing.
#
# shot(rect:) compares one region with a baseline PNG of that region only;
# whole screens are not compared any more (rake regress:live still may).
# Masks (s.mask) cover what moves on its own: the clock.
module Regress
  module Screens
    Scene = Struct.new(:name, :on, :block, :file, keyword_init: true)

    SCENES = [] # rubocop:disable Style/MutableConstant

    # Configuration names: the families with the standard build ("modern",
    # "retro"), the same with everything on mruby ("modern-mruby",
    # "retro-mruby"; FULL=1) and the browser build ("web").
    def self.scene(name, on:, &block)
      raise ArgumentError, "scene #{name}: a block is needed" unless block
      on = Array(on).map(&:to_s)
      raise ArgumentError, "scene #{name} is defined twice" if SCENES.any? { |s| s.name == name }
      SCENES << Scene.new(name: name, on: on, block: block, file: caller_locations(1, 1).first.path)
    end

    # Helpers every scene file may call on the session (s.open_editor ...).
    def self.helpers(&block)
      Session.class_eval(&block)
    end

    class Failure < StandardError; end

    # One scene on one configuration: what the block's `s` is.
    class Session
      attr_reader :config, :driver, :shots, :confirmed

      STABLE_GAP = 0.25   # seconds between captures while waiting
      SHOT_TIMEOUT = 8    # seconds for a shot to settle and match
      WAIT_TIMEOUT = 10
      QUIET = 1.0         # seconds without a change that make a screen settled

      def initialize(runner, config, scene, driver)
        @runner = runner
        @config = config
        @scene = scene
        @driver = driver
        @masks = []
        @shots = []
        @confirmed = []
        @mark = nil
        @last = nil
      end

      # --- what the scene can ask -------------------------------------------

      def name = @config.name
      def scene_name = @scene.name
      def family = @config.family
      def web? = @config.kind == :web
      def sim? = @config.kind == :sim
      def native? = @config.kind == :native
      def mruby? = @config.engine == "mruby"
      def w = @driver.width
      def h = @driver.height

      # --- input ------------------------------------------------------------

      def key(name) = input("key", name)
      def text(str) = input("text", str)
      def click(x, y) = input("click", x, y)
      def move(x, y) = input("move", x, y)
      def press(x, y) = input("down", x, y)
      def release(x, y) = input("up", x, y)
      def wheel(n) = input("wheel", n)

      # The screen just before the input is what a following wait_change
      # compares with (unless the scene marked one itself).
      def input(*words)
        @pre = capture unless @mark
        @driver.input(words.map(&:to_s))
        @touched = true
        @last = nil
      end

      # Whether the scene changed anything (the browser build is reloaded
      # after such a scene: it has no process list to close apps by).
      def touched? = @touched

      # Whether the scene compared or confirmed anything (a scene that does
      # not fails).
      def compared? = !(@shots.empty? && @confirmed.empty?)

      # The desktop as it was after the boot (the runner's capture of it).
      def desktop_at_rest = @runner.desktop

      # The last capture the scene took (written next to a failure).
      def last_capture = @last

      # Note something the scene has seen to hold (what a passing scene
      # confirmed; the waits below note theirs).
      def confirm(what)
        @confirmed << what
        what
      end

      # Open an app by its path: the sim's and the native program's debug
      # server, the browser build's launch slot (fmrb_web.rb launch). The
      # app's pid; a path that does not open fails the scene.
      def launch(path)
        @pre = capture unless @mark
        pid = begin
          @driver.launch(path)
        rescue DriverError => e
          raise Failure, "#{path} did not open (#{e.message.strip.lines.last&.strip})"
        end
        @last = nil
        pid
      end

      # The apps running now ({pid => name}), or nil where there is no
      # process list (the browser build).
      def apps
        @driver.respond_to?(:apps) ? @driver.apps : nil
      end

      def process_list? = !web?

      # Until the app is in the process list (it came up and did not quit).
      def wait_running(pid, what, timeout: WAIT_TIMEOUT)
        return confirm("#{what} opened (pid #{pid})") unless process_list?
        deadline = now + timeout
        loop do
          return confirm("#{what} is running (pid #{pid})") if apps.key?(pid)
          raise Failure, "#{what} (pid #{pid}) is not in the process list within #{timeout}s" if now > deadline
          sleep STABLE_GAP
        end
      end

      # Close an app by its pid and wait until it has left the process list.
      def close_app(pid, what, timeout: WAIT_TIMEOUT)
        raise Failure, "close_app: the browser build has no process list" unless process_list?
        @driver.kill(pid)
        deadline = now + timeout
        loop do
          return confirm("#{what} closed") unless apps.key?(pid)
          raise Failure, "#{what} (pid #{pid}) is still in the process list #{timeout}s after the kill" if now > deadline
          sleep STABLE_GAP
        end
      end

      # The File Manager's fixture (fixtures.rb): a fixed tree under
      # /tmp/regress_fm and the File Manager's state pointing at it, so it
      # opens there and lists those files rather than /app. Put back after
      # the configuration's scenes.
      def seed_fm
        @driver.seed_fm!
      end

      # --- masks ------------------------------------------------------------

      # Leave a rectangle out of every comparison of this scene (and of the
      # waits): something that moves on its own, like the clock.
      def mask(x, y, mw, mh)
        @masks << [x, y, mw, mh]
      end

      attr_reader :masks

      # --- waiting on the screen ----------------------------------------------

      def capture
        @last = @driver.capture
      end

      # Until the screen has not changed (outside the masks) for quiet
      # seconds: an animation that pauses between frames is not taken for
      # a settled screen.
      def wait_stable(timeout: WAIT_TIMEOUT, rect: nil, quiet: QUIET)
        prev = capture
        since = now
        deadline = now + timeout
        loop do
          sleep STABLE_GAP
          cur = capture
          if cur.same?(prev, @masks, rect: rect)
            return cur if now - since >= quiet
          else
            since = now
            prev = cur
          end
          raise Failure, "the screen did not settle within #{timeout}s" if now > deadline
        end
      end

      # Remember the screen now, for a later wait_change (by default it is
      # the screen before the last input).
      def mark
        @mark = capture
      end

      # Until the rectangle (the whole screen without one) differs from the
      # marked screen, or from the screen before the last input. what: the
      # change the scene waits for, in its failure and in what it confirmed.
      def wait_change(rect = nil, timeout: WAIT_TIMEOUT, what: nil)
        ref = @mark || @pre || capture
        @mark = nil
        @pre = nil
        wait_differs(ref, rect, timeout: timeout, what: what)
      end

      # Until the rectangle differs from a picture the scene kept (the
      # desktop at rest, an earlier capture).
      def wait_differs(ref, rect = nil, timeout: WAIT_TIMEOUT, what: nil)
        deadline = now + timeout
        loop do
          cur = capture
          unless cur.same?(ref, @masks, rect: rect)
            confirm(what) if what
            return cur
          end
          raise Failure, "#{what ? "#{what}: " : ''}nothing changed#{rect ? " in #{rect.inspect}" : ''} within #{timeout}s" if now > deadline
          sleep STABLE_GAP
        end
      end

      # Until the rectangle shows the same as a picture the scene kept.
      def wait_same(ref, rect = nil, timeout: WAIT_TIMEOUT, what: nil)
        deadline = now + timeout
        loop do
          cur = capture
          if cur.same?(ref, @masks, rect: rect)
            confirm(what) if what
            return cur
          end
          if now > deadline
            count, box = ref.diff(cur, @masks, rect: rect)
            raise Failure, "#{what ? "#{what}: " : ''}#{count} px differ in #{box.inspect}#{rect ? " (looking at #{rect.inspect})" : ''} after #{timeout}s"
          end
          sleep STABLE_GAP
        end
      end

      # Until the screen shows something: not one colour, not (almost) all
      # black. What a boot that hangs or a dead display gives.
      def wait_not_blank(what = "the screen", timeout: WAIT_TIMEOUT)
        deadline = now + timeout
        loop do
          colours, black = capture.colour_stats
          return confirm("#{what} is not blank (#{colours >= 16 ? '16+' : colours} colours)") if colours >= 2 && black < 0.95
          raise Failure, "#{what} is blank (#{colours} colour#{colours == 1 ? '' : 's'}, #{(black * 100).round}% black) after #{timeout}s" if now > deadline
          sleep STABLE_GAP
        end
      end

      # Until the block says yes for a capture (it gets the Image).
      def wait_until(what, timeout: WAIT_TIMEOUT)
        deadline = now + timeout
        loop do
          cur = capture
          if yield(cur)
            confirm(what)
            return cur
          end
          raise Failure, "timed out after #{timeout}s waiting for #{what}" if now > deadline
          sleep STABLE_GAP
        end
      end

      # Until a pixel has a colour ([r, g, b]).
      def wait_pixel(x, y, rgb, timeout: WAIT_TIMEOUT)
        wait_until("(#{x},#{y}) to be #{rgb.inspect}", timeout: timeout) { |img| img.pixel(x, y) == rgb }
      end

      # Until the core's log (sim only) has a line matching re since the
      # scene began.
      def wait_log(re, timeout: WAIT_TIMEOUT)
        raise Failure, "wait_log: the browser build has no log here" if web?
        deadline = now + timeout
        loop do
          return confirm("the log said #{re.inspect}") if @driver.log_since_mark =~ re
          raise Failure, "timed out after #{timeout}s waiting for the log to say #{re.inspect}" if now > deadline
          sleep 0.5
        end
      end

      # A plain pause, for the rare case no picture tells when to go on.
      def pause(ms)
        sleep ms / 1000.0
        @last = nil
      end

      # --- the comparison of a region -------------------------------------------

      # Settle, then compare the region rect ([x, y, w, h]) with its baseline
      # (a PNG of that region only), waiting up to timeout for the region to
      # come to it. The region's capture is saved either way; a missing
      # baseline counts as a failure until rake regress:screens:update. Keep
      # out of a region what changes with the theme or the menu bar: the
      # bar, the taskbar, the clock, the free memory, a Window's frame and
      # title. Without rect (rake regress:live only) the whole screen.
      def shot(label = nil, timeout: SHOT_TIMEOUT, rect: nil, quiet: QUIET)
        id = label ? "#{@scene.name}.#{label}" : @scene.name
        raise Failure, "shot #{id} twice in one scene" if @shots.any? { |s| s[:id] == id }
        raise Failure, "shot #{id}: a region (rect:) is needed; whole screens are not compared" if rect.nil? && !@runner.whole_shots?
        base_path = @runner.baseline_path(@config, id)
        base = File.exist?(base_path) ? Image.load(base_path) : nil
        masks = rect ? @masks.map { |x, y, mw, mh| [x - rect[0], y - rect[1], mw, mh] } : @masks.dup
        part = ->(img) { rect ? img.crop(rect) : img }
        deadline = now + timeout
        img = begin
          wait_stable(timeout: timeout, rect: rect, quiet: quiet)
        rescue Failure
          @last
        end
        if base
          until part.call(img).same?(base, masks) || now > deadline
            sleep STABLE_GAP
            img = capture
          end
        end
        res = @runner.record_shot(@config, id, part.call(img), base, masks, nil)
        res["rect"] = rect if rect
        @shots << res
        res
      end

      private

      def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    end
  end
end
