# frozen_string_literal: true
#
# games.rb - a game host of our own (asterism-games, README "Running") with
# one table, on the sim's zenohd. The table apps (othello, board, mahjong)
# list it, join it and play; the scene reads the table as the host has it.
#
#   g = s.counterpart :games, table: :othello
#   g.table            # the table's view (the host's `table`)
#   g.member           # the device's entry in the saved state (token, seat)
#   g.call(:look, g.token)   # any method of the table object
#   g.stop / g.resume  # SIGTERM (the host saves) / start it again on the
#                      # same state: the same seats and tokens
#
# The host runs from the asterism-games checkout next to fmruby-core
# (ASTERISM_GAMES_DIR elsewhere) with `bundle exec bin/asterism-games`, a
# configuration written under tmp/regress/live/ and a state directory there
# that is emptied when the counterpart starts. Its environment has no AI
# keys (no AI player is offered: the table's view stays the same), no
# credentials and the router set to the local one.
require "json"
require "yaml"

module Regress
  module Counterparts
    class Games < Base
      NODE = "rghost"
      APP = "games"
      GAMES_DIR = File.expand_path(ENV["ASTERISM_GAMES_DIR"] || File.join(ROOT, "asterism-games"))
      # What the environment must not pass to the host (an outer bundle
      # too: the host has its own Gemfile).
      DROP_ENV = /\A(BUNDLE_.*|RUBYOPT|RUBYLIB|ANTHROPIC_API_KEY|TYPESAFE_API_KEY|OPENAI_API_KEY|ASTERISM_GAMES_.*|MAHJONG_.*|OTHELLO_.*|BOARD_.*)\z/
      # One table each; short CPU pauses so a scene does not wait on them.
      TABLES = {
        othello: { "name" => "othello1", "game" => "othello", "cpu_ms" => 200 },
        board: { "name" => "board1", "game" => "board" },
        mahjong: { "name" => "mahjong1", "game" => "mahjong", "cpu_ms" => 300,
                   "rule" => { "game_type" => "tonpuu" } }
      }.freeze

      attr_reader :name

      def initialize(dir, table:, **opts)
        super(dir, **opts)
        @spec = TABLES.fetch(table.to_sym) { raise Error, "games: no table #{table.inspect} (#{TABLES.keys.join(', ')})" }
        @name = @spec["name"]
        @state_dir = File.join(dir, "state")
        @config = File.join(dir, "config.yml")
      end

      def path = "#{NODE}/#{APP}/#{@name}"

      def start
        bin = File.join(GAMES_DIR, "bin", "asterism-games")
        raise Error, "games: no asterism-games at #{GAMES_DIR} (set ASTERISM_GAMES_DIR)" unless File.exist?(bin)
        FileUtils.rm_rf(@state_dir)
        FileUtils.mkdir_p(@state_dir)
        File.write(@config, YAML.dump("router" => ROUTER, "node" => NODE, "app" => APP,
                                      "state_dir" => @state_dir, "tables" => [@spec]))
        resume
      end

      # Start the host on the state it saved (the first time: an empty one).
      def resume
        raise Error, "games: the host is already running" if @pid
        Counterparts.wait_for("zenohd on 127.0.0.1:7447", 30) { Counterparts.listening?(7447) }
        # spawn adds to the environment: a key set to nil is taken out.
        env = ENV.keys.grep(DROP_ENV).to_h { |k| [k, nil] }
        env["ASTERISM_GAMES_CONFIG"] = @config
        log = File.join(@dir, "host.log")
        from = File.exist?(log) ? File.size(log) : 0
        @pid, @log = spawn_logged("host", env.merge("BUNDLE_GEMFILE" => File.join(GAMES_DIR, "Gemfile")),
                                  %w[bundle exec bin/asterism-games], GAMES_DIR)
        Counterparts.wait_for("the game host (#{@log})", 60) do
          text = log_since(@log, from)
          raise Error, "games: the host exited (#{text.lines.last(3).join.strip})" if Process.wait(@pid, Process::WNOHANG)
          text.include?(" up (")
        end
        # The table answers (the probe sees the host).
        Counterparts.wait_for("#{path} to answer", 20) { (table rescue nil) } # rubocop:disable Style/RescueModifier
        self
      end

      # SIGTERM: the host saves the tables and leaves the network.
      def stop
        kill_group(@pid, "TERM", wait: 15)
        @pid = nil
      end

      def close
        stop if @pid
      end

      def running? = !@pid.nil?

      def call(method, *args, timeout: 3.0)
        Probe.call(path, method, *args, timeout: timeout)
      end

      def table = call(:table)
      def state = table

      # The table as the host saved it (state_dir/<table>.json).
      def saved
        f = File.join(@state_dir, "#{@name}.json")
        File.exist?(f) ? JSON.parse(File.read(f)) : {}
      end

      # The device's entry among the saved members (node "linux" on the sim,
      # whatever it is: the only member that is not ours).
      def member
        saved.fetch("members", {}).values.first
      end

      def token = member&.fetch("token", nil)
    end

    register :games, Games
  end
end
