# frozen_string_literal: true
#
# counterparts.rb - the other side of a live scene (rake regress:live,
# doc/regression/usage.md chapter 10): something outside the machine that an
# app talks to, started by the tool for one scene and taken down after it.
#
#   s.counterpart :games, table: :othello   # a local game host with one table
#   s.counterpart :steps                    # a CRuby step sender (Asterism)
#   s.counterpart :midi                     # the serial MIDI monitor
#   s.counterpart :audio                    # the APU's samples (the probe)
#
# Each kind is a small class with the same four verbs: start (bring it up),
# state (what it saw or holds now: a Hash a scene can summarise), stop and
# resume (take it away and bring it back in the middle of a scene: the game
# host restarting), plus close (the runner's, at the end of the scene, also
# after a failure). A kind that cannot stop and come back says so.
#
# Everything runs on this PC: the sim's own zenohd (tcp 127.0.0.1:7447), a
# game host from the asterism-games checkout next to fmruby-core with a
# configuration of our own (no credentials, no router but the local one),
# and the root tools (fmrb_midi_monitor.rb, fmrb_audio_probe.rb) and
# tool/imu/step_send.rb as they are.
require "fileutils"
require "socket"

module Regress
  module Counterparts
    ROOT = File.expand_path("../../..", __dir__) # family-mruby
    CORE = File.join(ROOT, "fmruby-core")
    ROUTER = "tcp/127.0.0.1:7447"

    class Error < StandardError; end

    KINDS = {} # rubocop:disable Style/MutableConstant

    def self.register(name, klass)
      KINDS[name.to_sym] = klass
    end

    def self.build(name, dir, **opts)
      klass = KINDS[name.to_sym] or raise Error, "no counterpart #{name.inspect} (#{KINDS.keys.join(', ')})"
      klass.new(dir, **opts)
    end

    def self.listening?(port)
      Socket.tcp("127.0.0.1", port, connect_timeout: 0.5).close
      true
    rescue StandardError
      false
    end

    def self.now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

    def self.wait_for(what, timeout, every: 0.25)
      deadline = now + timeout
      loop do
        v = yield
        return v if v
        raise Error, "timed out after #{timeout}s waiting for #{what}" if now > deadline
        sleep every
      end
    end

    # One CRuby Asterism connection for the whole run, made when a
    # counterpart first needs to call an object (the asterism gem: what
    # tool/imu/step_send.rb needs too).
    module Probe
      NODE = "rgprobe"

      module_function

      def connect
        return if @up
        require "asterism"
        Counterparts.wait_for("zenohd on 127.0.0.1:7447", 30) { Counterparts.listening?(7447) }
        Asterism.connect(ROUTER, node: NODE, app: "regress")
        @up = true
      end

      def call(path, method, *args, timeout: 3.0)
        connect
        Asterism[path, timeout: timeout].public_send(method, *args)
      end

      def close
        return unless @up
        Asterism.close
        @up = false
      rescue StandardError
        @up = false
      end
    end

    class Base
      attr_reader :dir

      def initialize(dir, **opts)
        @dir = dir
        @opts = opts
        FileUtils.mkdir_p(dir)
      end

      def kind = self.class.name.split("::").last.downcase
      def start; end
      def state = {}
      def stop = raise(Error, "#{kind}: cannot stop and come back")
      def resume = raise(Error, "#{kind}: cannot stop and come back")
      def close; end

      private

      # A process in its own group, its output to a log under dir.
      def spawn_logged(name, env, cmd, chdir)
        log = File.join(@dir, "#{name}.log")
        pid = Process.spawn(env, *cmd, chdir: chdir, in: File::NULL, out: [log, "a"], err: %i[child out], pgroup: true)
        [pid, log]
      end

      def kill_group(pid, sig = "TERM", wait: 10)
        return unless pid
        begin
          Process.kill(sig, -pid)
        rescue Errno::ESRCH
          nil
        end
        deadline = Counterparts.now + wait
        loop do
          return if Process.wait(pid, Process::WNOHANG)
          if Counterparts.now > deadline
            Process.kill("KILL", -pid) rescue nil # rubocop:disable Style/RescueModifier
            Process.wait(pid) rescue nil # rubocop:disable Style/RescueModifier
            return
          end
          sleep 0.1
        end
      rescue Errno::ECHILD
        nil
      end

      def log_since(log, start)
        return "" unless File.exist?(log)
        File.open(log, "rb") do |f|
          f.seek(start)
          f.read.to_s.force_encoding("UTF-8")
        end
      end
    end
  end
end

Dir[File.join(__dir__, "counterparts", "*.rb")].sort.each { |f| require f }
