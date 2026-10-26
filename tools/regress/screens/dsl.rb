# frozen_string_literal: true
#
# dsl.rb - how a screen scene is written (test/regress/screens/*.rb,
# doc/regression/usage.md chapter 9).
#
#   Regress::Screens.scene "editor_type", on: %w[modern retro web] do |s|
#     s.open_editor                 # helpers from test/regress/screens/helpers.rb
#     s.key "a"
#     s.wait_change [0, 20, 100, 30]
#     s.shot
#   end
#
# A scene runs on every configuration it names (on:), each time from the
# desktop with no app open (the runner closes what a scene opened). Waiting
# is on the screen, not on the clock: shot waits until two captures in a row
# agree (outside the masks) and then, when there is a baseline, until the
# screen agrees with it, up to a limit. Masks (s.mask) cover what moves on
# its own: the clock, the free memory.
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
      attr_reader :config, :driver, :shots

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

      # Whether the scene compared anything (a scene that does not fails).
      def compared? = !@shots.empty?

      # Open an app by its path (the sim's debug server; the browser build
      # has no such door, so a web scene goes through the launcher instead).
      def launch(path)
        raise Failure, "launch(#{path}): the browser build cannot open an app by path; use the launcher" if web?
        @driver.launch(path)
        @last = nil
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
      # marked screen, or from the screen before the last input.
      def wait_change(rect = nil, timeout: WAIT_TIMEOUT)
        ref = @mark || @pre || capture
        @mark = nil
        @pre = nil
        deadline = now + timeout
        loop do
          cur = capture
          return cur unless cur.same?(ref, @masks, rect: rect)
          raise Failure, "nothing changed#{rect ? " in #{rect.inspect}" : ''} within #{timeout}s" if now > deadline
          sleep STABLE_GAP
        end
      end

      # Until the block says yes for a capture (it gets the Image).
      def wait_until(what, timeout: WAIT_TIMEOUT)
        deadline = now + timeout
        loop do
          cur = capture
          return cur if yield(cur)
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
          return true if @driver.log_since_mark =~ re
          raise Failure, "timed out after #{timeout}s waiting for the log to say #{re.inspect}" if now > deadline
          sleep 0.5
        end
      end

      # A plain pause, for the rare case no picture tells when to go on.
      def pause(ms)
        sleep ms / 1000.0
        @last = nil
      end

      # --- the comparison -------------------------------------------------------

      # Settle, then compare with the baseline (waiting up to timeout for the
      # screen to come to it). The capture is saved either way; a missing
      # baseline counts as a failure until rake regress:screens:update.
      def shot(label = nil, timeout: SHOT_TIMEOUT, rect: nil, quiet: QUIET)
        id = label ? "#{@scene.name}.#{label}" : @scene.name
        raise Failure, "shot #{id} twice in one scene" if @shots.any? { |s| s[:id] == id }
        base_path = @runner.baseline_path(@config, id)
        base = File.exist?(base_path) ? Image.load(base_path) : nil
        deadline = now + timeout
        img = begin
          wait_stable(timeout: timeout, rect: rect, quiet: quiet)
        rescue Failure
          @last
        end
        if base
          until img.same?(base, @masks, rect: rect) || now > deadline
            sleep STABLE_GAP
            img = capture
          end
        end
        res = @runner.record_shot(@config, id, img, base, @masks.dup, rect)
        @shots << res
        res
      end

      private

      def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    end
  end
end
