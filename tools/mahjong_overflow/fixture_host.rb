#!/usr/bin/env ruby
# frozen_string_literal: true
#
# fixture_host.rb - a stand-in game host that serves the worst cases of
# fixtures.rb to the real table app, for looking at them (screenshots of
# doc/ruby_asterism/report/mj5.md). It answers like a table (join, view,
# act ... ) with the case named in a file, and puts its state every second.
#
#   cd ../asterism-games && bundle exec ruby ../fmruby-core/tools/mahjong_overflow/fixture_host.rb \
#     [--router tcp/127.0.0.1:7447] [--case-file /tmp/mj_case]
#   echo result_triple_ron > /tmp/mj_case     # the case to show (fixtures.rb)
#
# The table is games/mahjongfx under the node "fixtures": open the table app,
# choose fixtures/mahjongfx in the list and Enter. A change of the case file
# shows on the screen within a second (the version goes up).

require "asterism"
require_relative "fixtures"

router = "tcp/127.0.0.1:7447"
case_file = "/tmp/mj_case"
args = ARGV.dup
until args.empty?
  a = args.shift
  case a
  when "--router" then router = args.shift
  when "--case-file" then case_file = args.shift
  else abort "unknown argument #{a}"
  end
end

CASES = MjFixtures.all

# The table object: every view is the case's, with a version that moves on
# when the case changes.
class FixtureTable
  def initialize(case_file)
    @case_file = case_file
    @ver = 1000
    @name = nil
  end

  def current
    n = File.exist?(@case_file) ? File.read(@case_file).strip : ""
    n = CASES.keys.first unless CASES.key?(n)
    if n != @name
      @name = n
      @ver += 10
      warn "fixture: #{n}"
    end
    v = Marshal.load(Marshal.dump(CASES[n][0]))
    v["v"] = @ver
    v["t"] = "mahjongfx"
    v
  end

  # A secret (a browser in a room) is taken and never printed; only that one came.
  def join(_name, secret = nil)
    warn "fixture: join (#{secret ? 'with' : 'without'} a secret)"
    { "token" => "fixture-token", "seat" => current["me"].to_i, "owner" => true }
  end

  def mine(_secret = nil) = join(nil, _secret)
  def sit(*) = "ok"
  def stand(*) = "ok"
  def cpu(*) = "ok"
  def start(*) = "ok"
  def rule(*) = "ok"
  def act(*) = "ok"
  def leave(*) = true
  def view(_token, _assist = nil) = current

  def table
    v = current
    %w[me h dt a wt uk sh sd].each { |k| v.delete(k) }
    v
  end
end

METHODS = { join: -1, mine: -1, sit: 2, stand: 1, cpu: 3, start: 1, rule: 3, view: -1, act: 4, table: 0, leave: 1 }.freeze

t = FixtureTable.new(case_file)
Asterism.connect(router, node: "fixtures", app: "games")
Asterism.expose("mahjongfx", t, methods: METHODS)
warn "fixture host: fixtures/games/mahjongfx via #{router}; cases: #{CASES.keys.join(' ')}"
stop = false
trap("TERM") { stop = true }
trap("INT") { stop = true }
put_at = 0
until stop
  break unless Asterism.poll
  now = Process.clock_gettime(Process::CLOCK_MONOTONIC, :millisecond)
  if now - put_at >= 1000
    put_at = now
    Asterism.session.put("asterism/fixtures/games/mahjongfx/state", Asterism::Codec.pack(t.table))
  end
  sleep 0.02
end
Asterism.close
