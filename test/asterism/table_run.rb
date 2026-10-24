#!/usr/bin/env ruby
# Host tests for FmrbAsterism::TableClient (doc/app_refactor, R4:
# lib/add/picoruby-fmrb-asterism/mrblib/table_client.rb), the device side
# of a game host's table, on top of FmrbAsterism::Link (link.rb).
#
# The shipped files run here against stand-ins: a fake Asterism with a
# fake game host behind it (its tables on the network, their state puts,
# join / view / leave, a host that leaves and comes back, calls that are
# not answered), a clock moved by hand and Log. The owner records the
# calls the client makes on it. Same tier as test/asterism/link_run.rb.
#
#   ruby test/asterism/table_run.rb     (part of rake test: asterism:table_test)
#
# What this cannot see: a real game host, the router and the screen. Those
# stay with the sim and the browser build (doc/app_refactor/report/r4a.md).

module Check
  @failed = 0
  def self.failed = @failed
  def self.fail! = @failed += 1
end

def check(what, got, want)
  if got == want
    puts "  ok   #{what}"
  else
    puts "  FAIL #{what}: got #{got.inspect}, want #{want.inspect}"
    Check.fail!
  end
end

ROOT = File.expand_path("../..", __dir__)
DIR = "#{ROOT}/lib/add/picoruby-fmrb-asterism/mrblib"

module FmrbConst
  PLATFORM = "linux"
end

module Machine
  @ms = 0
  def self.board_millis = @ms
  def self.ms=(v)
    @ms = v
  end
  def self.advance(d) = @ms += d
end

module Log
  @lines = []
  def self.lines = @lines
  def self.info(s) = @lines << "I #{s}"
  def self.warn(s) = @lines << "W #{s}"
end

class FmrbApp
  def self.wifi_info = nil
end

# The game host: its tables and what they answer. A call's answer is
# ready at once unless hold is set (then release makes it ready); a host
# that is away answers nothing (the future raises when its value is read).
class FakeHost
  attr_accessor :away, :hold, :refuse, :drop, :version
  attr_reader :calls, :tables

  def initialize
    @calls = []
    @tables = ["h/games/othello1", "h/games/othello2", "h/games/board1"]
    @away = false
    @hold = false
    @refuse = nil
    @drop = false
    @version = 1
    @held = []
  end

  def call(path, name, args)
    @calls << [path, name, *args]
    f = FakeFuture.new
    if @away
      f.fail = true
    else
      f.result = answer(name, args)
    end
    if @hold
      @held << f
    else
      f.ready = true
    end
    f
  end

  def release
    @held.each { |f| f.ready = true }
    @held = []
  end

  def answer(name, args)
    case name
    when :join then @refuse ? { "error" => @refuse } : { "token" => "tok-#{args[0]}", "seat" => -1 }
    when :view then @drop ? { "error" => "token" } : { "v" => @version, "me" => -1, "st" => "wait" }
    when :leave then true
    else "ok"
    end
  end

  def count(name) = @calls.count { |c| c[1] == name }
end

class FakeFuture
  attr_accessor :ready, :result, :fail

  def done? = @ready
  def took_ms = 5

  def value
    raise Asterism::Error, "timeout" if @fail
    @result
  end
end

class FakeAsync
  def initialize(host, path)
    @host = host
    @path = path
  end

  def method_missing(name, *args)
    @host.call(@path, name, args)
  end

  def respond_to_missing?(*) = true
end

class FakeProxy
  attr_reader :asterism_path

  def initialize(host, path)
    @host = host
    @asterism_path = path
  end

  def async = FakeAsync.new(@host, @asterism_path)
end

class FakeSub
  attr_reader :key

  def initialize(key)
    @key = key
    @queue = []
  end

  def push(key, payload) = @queue << [key, payload]

  def each_pending
    q = @queue
    @queue = []
    q.each { |k, p| yield k, p, nil }
  end
end

class FakeSession
  attr_reader :subs

  def initialize = @subs = []

  def subscribe(key, depth: 1)
    s = FakeSub.new(key)
    @subs << s
    s
  end
end

module Asterism
  class Error < StandardError; end

  module Codec
    def self.unpack(payload) = payload
  end

  class << self
    attr_accessor :host, :poll_plan, :reconnecting_now, :on_join_block, :on_leave_block
    attr_reader :calls, :session

    def reset
      @calls = []
      @poll_plan = []
      @connected = false
      @reconnecting_now = false
      @session = FakeSession.new
      @on_join_block = nil
      @on_leave_block = nil
      @host = FakeHost.new
    end

    def connect(locator = nil, node: nil, app:, mode: nil, listen: nil, credentials: nil)
      @calls << [:connect, app]
      @connected = true
      @session = FakeSession.new
      self
    end

    def close
      @calls << [:close]
      @connected = false
      @on_join_block = nil
      @on_leave_block = nil
      nil
    end

    def poll
      @calls << [:poll]
      r = @poll_plan.shift
      r = true if r.nil?
      @connected = false unless r
      r
    end

    def on_join(&b) = @on_join_block = b
    def on_leave(&b) = @on_leave_block = b
    def connected? = @connected
    def reconnecting? = @reconnecting_now
    def lost_reason = "the connection was lost"

    def each(pattern)
      @calls << [:each, pattern]
      @host.away ? [] : @host.tables.map { |p| FakeProxy.new(@host, p) }
    end

    def [](path, timeout: nil) = FakeProxy.new(@host, path)

    # A state put of a table, as the host makes it.
    def put_state(path, value)
      s = @session.subs.find { |x| x.key == "asterism/*/games/*/state" }
      s&.push("asterism/#{path}/state", value)
    end

    def count(kind) = @calls.count { |c| c[0] == kind }
  end
end

Settings = Struct.new(:node, :router, :credentials, :error)

load "#{DIR}/link.rb"
load "#{DIR}/table_client.rb"

# The application: every call the client makes on it, in order.
class Owner
  attr_reader :log
  attr_accessor :tc

  def initialize = @log = []

  def table_state(p, pv)
    @log << [:state, p, pv["st"]]
    [:summary, pv["st"]]
  end

  def table_joined(r) = @log << [:joined, r["token"], tc.token]
  def table_refused(why) = @log << [:refused, why, tc.table]
  def table_view(v) = @log << [:view, v["v"]]
  def table_link_up(link) = @log << [:up, link.state]
  def table_link_down(link) = @log << [:down, link.state]
  def table_watch = @log << [:watch, Asterism.count(:poll)]
  def tables_changed(added) = @log << [:changed, added, tc.table_list.size]
  def table_host_gone = @log << [:gone]
  def table_host_back = @log << [:back]
  def table_view_failed = @log << [:view_failed]
  def table_dropped = @log << [:dropped]

  def kinds = @log.map { |e| e[0] }
  def last(kind) = @log.reverse.find { |e| e[0] == kind }
  def count(kind) = @log.count { |e| e[0] == kind }
end

# Only the required calls (the optional ones must not be called).
class Minimal
  attr_reader :log

  def initialize = @log = []
  def table_state(p, pv) = pv["st"]
  def table_joined(r) = @log << :joined
  def table_refused(why) = @log << [:refused, why]
  def table_view(v) = @log << :view
end

def fresh(owner: Owner.new, kind: "othello", **opts)
  Asterism.reset
  Log.lines.clear
  Machine.ms = 1000
  tc = FmrbAsterism::TableClient.new(owner, kind, tag: "t", settings: Settings.new("nodeA", "tcp/192.0.2.2:7447", nil, nil),
                                     **opts)
  owner.tc = tc if owner.respond_to?(:tc=)
  [tc, owner]
end

# Connected, the tables found, at othello1 with a token.
def seated(**opts)
  tc, owner = fresh(**opts)
  tc.update
  tc.update
  tc.join("h/games/othello1", "me")
  tc.update
  tc.update_view
  [tc, owner]
end

puts "connecting: the states' subscription, on_join / on_leave, the list"
tc, owner = fresh
check("defaults", [tc.view_every_ms, tc.view_min_ms, tc.list_every_ms, tc.rescan_ms, tc.rescan_step_ms,
                   tc.host_gone_view_ms, tc.call_timeout], [3000, 0, 30000, 3000, 300, 30000, 3.0])
check("before: connecting", tc.net, :connecting)
check("first update: not connected yet (Link's :connecting)", [tc.update, tc.link.state], [false, :connecting])
check("second update connects", tc.update, true)
check("the app's name on the network", Asterism.calls.find { |c| c[0] == :connect }, [:connect, "game"])
check("net up", tc.net, :up)
check("on_join and on_leave given", [Asterism.on_join_block.nil?, Asterism.on_leave_block.nil?], [false, false])
check("the states subscribed", Asterism.session.subs.map(&:key), ["asterism/*/games/*/state"])
check("table_link_up, then the watch, in the update that connects", owner.kinds.first(2), [:up, :watch])
check("only the game's tables on the list", tc.table_list, ["h/games/othello1", "h/games/othello2"])
check("tables_changed(true) for each", owner.log.select { |e| e[0] == :changed }, [[:changed, true, 1], [:changed, true, 2]])
check("a table not put yet keeps nil", tc.tables["h/games/othello1"], nil)
check("logged", Log.lines.include?("I t: table h/games/othello1 on the list"), true)

puts "the watch comes after the poll and before the states"
Asterism.put_state("h/games/othello1", { "st" => "wait", "v" => 3 })
owner.log.clear
tc.update
check("watch first (after this update's poll), then the state", owner.kinds.first(2), [:watch, :state])
check("watch saw the poll", owner.last(:watch)[1], Asterism.count(:poll))

puts "state puts"
tc, owner = fresh
tc.update
tc.update
Asterism.put_state("h/games/othello2", { "st" => "play", "v" => 7 })
Asterism.put_state("h/games/board1", { "st" => "x", "v" => 1 })
Asterism.put_state("h/games/othello9", { "st" => "over", "v" => 1 })
Asterism.put_state("h/games/othello3", "not a hash")
owner.log.clear
tc.update
check("only the game's Hash puts reach table_state", owner.log.select { |e| e[0] == :state },
      [[:state, "h/games/othello2", "play"], [:state, "h/games/othello9", "over"]])
check("its answer is what the list keeps", tc.tables["h/games/othello2"], [:summary, "play"])
check("a table known from its put alone joins the list", tc.table_list.include?("h/games/othello9"), true)
check("not at a table: want_v stays", tc.want_v, 0)

puts "the scan: on_join for rescan_ms every rescan_step_ms, else every list_every_ms"
tc, owner = fresh
tc.update
tc.update
n = Asterism.count(:each)
Machine.advance(299)
tc.update
check("not before 300 ms", Asterism.count(:each), n)
Machine.advance(1)
tc.update
check("at 300 ms", Asterism.count(:each), n + 1)
Machine.advance(3000)
tc.update
n = Asterism.count(:each)
Machine.advance(300)
tc.update
check("after rescan_ms: no scan at 300 ms", Asterism.count(:each), n)
Machine.advance(29_700)
tc.update
check("every list_every_ms", Asterism.count(:each), n + 1)
Asterism.on_join_block.call("other")
tc.update
check("on_join scans at once", Asterism.count(:each), n + 2)
tc.list_every_ms = 1000
Machine.advance(3000)
tc.update
n = Asterism.count(:each)
Machine.advance(1000)
tc.update
check("the app may change the periods", Asterism.count(:each), n + 1)

puts "join, the token, the view"
tc, owner = fresh
tc.update
tc.update
Asterism.host.hold = true
check("join sends and answers nil", tc.join("h/games/othello1", "me"), nil)
check("joining", [tc.joining?, tc.table, tc.token], [true, "h/games/othello1", nil])
check("a second join while one is on its way does nothing", tc.join("h/games/othello2", "me"), nil)
check("one join call", Asterism.host.count(:join), 1)
check("nil path does nothing", tc.join(nil, "me"), nil)
tc.update
check("not answered yet: still joining", [tc.joining?, owner.count(:joined)], [true, 0])
Asterism.host.hold = false
Asterism.host.release
tc.update
check("joined: token set before table_joined", owner.last(:joined), [:joined, "tok-me", "tok-me"])
check("the view asked right after", Asterism.host.calls.last, ["h/games/othello1", :view, "tok-me"])
check("logged", Log.lines.include?("I t: joined h/games/othello1 (5 ms)"), true)
tc.update_view
check("the view reaches the app", owner.last(:view), [:view, 1])
check("view_v", tc.view_v, 1)

puts "when the view is asked"
tc, owner = seated
n = Asterism.host.count(:view)
Machine.advance(2999)
tc.update
tc.update_view
check("not before view_every_ms", Asterism.host.count(:view), n)
Machine.advance(1)
tc.update_view
check("at view_every_ms", Asterism.host.count(:view), n + 1)
tc.update_view
Asterism.host.version = 2
Asterism.put_state("h/games/othello1", { "st" => "wait", "v" => 2 })
tc.update
check("a newer version: want_v", tc.want_v, 2)
tc.update_view
check("a newer version asks at once (view_min_ms 0)", Asterism.host.count(:view), n + 2)
tc.update_view
check("the view of v2: no more", [tc.view_v, Asterism.host.count(:view)], [2, n + 2])

tc, owner = seated
tc.view_min_ms = 1000
tc.update_view
n = Asterism.host.count(:view)
Asterism.put_state("h/games/othello1", { "st" => "wait", "v" => 5 })
tc.update
tc.update_view
check("view_min_ms: a newer version waits", Asterism.host.count(:view), n)
Machine.advance(1000)
tc.update_view
check("view_min_ms: asked once it has gone by", Asterism.host.count(:view), n + 1)

puts "refused and not answered"
tc, owner = fresh
tc.update
tc.update
Asterism.host.refuse = "full"
tc.join("h/games/othello1", "me")
tc.update
check("refused: the host's word, table cleared", owner.last(:refused), [:refused, "full", nil])
check("no token", tc.token, nil)
Asterism.host.refuse = nil
Asterism.host.away = true
tc.join("h/games/othello1", "me")
tc.update
check("not answered: nil", owner.last(:refused), [:refused, nil, nil])

puts "leave"
tc, owner = seated
tc.leave
check("the host told", Asterism.host.calls.last, ["h/games/othello1", :leave, "tok-me"])
check("off the table", [tc.table, tc.token, tc.view_v, tc.host_gone?], [nil, nil, 0, false])
n = Asterism.host.count(:view)
Machine.advance(5000)
tc.update
tc.update_view
check("no view off the table", Asterism.host.count(:view), n)
tc, owner = seated
tc.forget
check("forget does not tell the host", [Asterism.host.count(:leave), tc.table], [0, nil])

puts "the host leaves the network and comes back"
tc, owner = seated
Asterism.on_leave_block.call("h")
check("its tables off the list", tc.table_list, [])
check("tables_changed(false)", owner.last(:changed), [:changed, false, 0])
check("at its table: host gone", [tc.host_gone?, owner.count(:gone)], [true, 1])
check("still at the table", [tc.table, tc.token], ["h/games/othello1", "tok-me"])
check("logged", Log.lines.include?("I t: h left the network: 2 table(s) off the list"), true)
Asterism.on_leave_block.call("someone")
check("another node leaving changes nothing", [owner.count(:changed), owner.count(:gone)], [3, 1])
n = Asterism.host.count(:view)
Machine.advance(29_000)
tc.update_view
check("host gone: no view at the usual period", Asterism.host.count(:view), n)
Machine.advance(1000)
tc.update_view
check("host gone: a view every host_gone_view_ms", Asterism.host.count(:view), n + 1)
Asterism.on_join_block.call("h")
check("back: table_host_back", [tc.host_gone?, owner.count(:back)], [false, 1])
tc.update
check("back: the list again", tc.table_list, ["h/games/othello1", "h/games/othello2"])
tc.update_view
check("back: the view asked at once", Asterism.host.count(:view), n + 2)
check("logged", Log.lines.include?("I t: h is back"), true)
Asterism.on_join_block.call("h")
check("a join while not gone: no second back", owner.count(:back), 1)

puts "a view sent after the host left and answered: back without on_join"
tc, owner = seated
Asterism.on_leave_block.call("h")
Machine.advance(30_000)
Asterism.host.hold = true
tc.update_view
Asterism.host.hold = false
Asterism.host.release
Machine.advance(50)
tc.update_view
check("host_gone cleared, no table_host_back", [tc.host_gone?, owner.count(:back)], [false, 0])

puts "a view not answered, a view refused (the host forgot us)"
tc, owner = seated
Asterism.host.away = true
tc.refresh_view
tc.update_view
check("table_view_failed", owner.count(:view_failed), 1)
check("still at the table", tc.table, "h/games/othello1")
Asterism.host.away = false
Asterism.host.drop = true
tc.refresh_view
tc.update_view
check("table_dropped", owner.count(:dropped), 1)
check("the app decides (still at the table here)", tc.table, "h/games/othello1")
tc, owner = seated(owner: Minimal.new)
Asterism.host.drop = true
Asterism.host.away = false
tc.refresh_view
tc.update_view
check("without table_dropped the client leaves", [tc.table, Asterism.host.count(:leave)], [nil, 1])

puts "the connection lost and back"
tc, owner = seated
Asterism.host.hold = true
tc.refresh_view
Asterism.poll_plan = [false]
check("the update that loses it returns false", tc.update, false)
check("table_link_down", owner.last(:down), [:down, :lost])
check("net down", tc.net, :down)
check("no update_view while down", (tc.update_view; Asterism.host.count(:view)), Asterism.host.count(:view))
check("still at the table (the app decides)", [tc.table, tc.token], ["h/games/othello1", "tok-me"])
Machine.advance(3000)
tc.update
check("retry_ms later: Link connects", tc.update, true)
check("table_link_up again", owner.count(:up), 2)
check("subscribed again on the new session", Asterism.session.subs.map(&:key), ["asterism/*/games/*/state"])
check("on_join given again", Asterism.on_join_block.nil?, false)
Asterism.host.hold = false
Asterism.host.release
n = Asterism.host.count(:view)
tc.update_view
check("the view in flight was dropped: a new one is asked", Asterism.host.count(:view), n + 1)
Asterism.reconnecting_now = true
tc.update
check("reconnecting: still up, reconnecting?", [tc.net, tc.reconnecting?], [:up, true])

puts "an owner with the required calls only"
tc, owner = fresh(owner: Minimal.new)
tc.update
tc.update
Asterism.put_state("h/games/othello1", { "st" => "wait", "v" => 1 })
tc.update
Asterism.on_leave_block.call("h")
Asterism.on_join_block.call("h")
check("no optional call made; the host's tables left the list", tc.tables.keys, [])
tc.join("h/games/othello2", "me")
tc.update
tc.update_view
check("joined and the view", owner.log, [:joined, :view])

puts "the board's kind, the periods given, WiFi on the ESP32"
tc, owner = fresh(kind: "board")
tc.update
tc.update
check("board tables only", tc.table_list, ["h/games/board1"])
check("ours?", [tc.ours?("x/games/board2"), tc.ours?("x/games/othello1")], [true, false])
check("host_of", tc.host_of("x/games/board2"), "x")
check("WiFi: no wait off the ESP32", tc.link.instance_variable_get(:@wifi_wait_ms), 0)
FmrbConst.send(:remove_const, :PLATFORM)
FmrbConst.const_set(:PLATFORM, "esp32")
tc, owner = fresh
check("WiFi: Link's wait on the ESP32", tc.link.instance_variable_get(:@wifi_wait_ms), 20_000)
tc, owner = fresh(wifi_wait_ms: 500, retry_ms: 1000)
check("given wifi_wait_ms and retry_ms", [tc.link.instance_variable_get(:@wifi_wait_ms),
                                         tc.link.instance_variable_get(:@retry_ms)], [500, 1000])

puts
if Check.failed.zero?
  puts "all passed"
else
  puts "#{Check.failed} failed"
  exit 1
end
