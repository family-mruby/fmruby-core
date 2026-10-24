#!/usr/bin/env ruby
# Host tests for FmrbAsterism::Link (doc/app_refactor, R1:
# lib/add/picoruby-fmrb-asterism/mrblib/link.rb), the part that keeps an
# application connected.
#
# The shipped file runs here against stand-ins: a fake Asterism (connect
# succeeds, fails, or says "already on the network"; poll reports a loss or
# raises; reconnecting? can be set), a clock moved by hand
# (Machine.board_millis), FmrbApp.wifi_info, and Log. Same tier as
# test/assoc/run.rb: no docker, no device, no asterism checkout.
#
#   ruby test/asterism/link_run.rb      (part of rake test: asterism:link_test)
#
# What this cannot see: a real router, the TLS handshake's time and the
# screen. Those stay with the sim, the browser build and the boards
# (doc/app_refactor/report/r1.md).

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
LINK_FILE = "#{ROOT}/lib/add/picoruby-fmrb-asterism/mrblib/link.rb"

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
  @wifi = nil
  def self.wifi_info = @wifi
  def self.wifi=(v)
    @wifi = v
  end
end

module Asterism
  class Error < StandardError; end
  class Disconnected < Error; end

  @calls = []
  @fail = []          # errors the next connects raise, in order
  @poll = []          # what the next polls do: true, false, or an exception
  @connected = false
  @reconnecting = false
  @lost = nil

  class << self
    attr_accessor :fail, :poll_plan, :reconnecting_now, :lost_now
    attr_reader :calls

    def reset
      @calls = []
      @fail = []
      @poll_plan = []
      @connected = false
      @reconnecting_now = false
      @lost_now = nil
    end

    def connect(locator = nil, node: nil, app:, mode: nil, listen: nil, credentials: nil)
      @calls << [:connect, locator, node, app, mode, listen, credentials]
      raise Error, "already connected (Asterism.close first)" if @connected
      e = @fail.shift
      raise e if e
      @connected = true
      self
    end

    def close
      @calls << [:close]
      @connected = false
      nil
    end

    def poll
      @calls << [:poll]
      r = @poll_plan.shift
      r = true if r.nil?
      raise r if r.is_a?(Exception)
      @connected = false unless r
      r
    end

    def connected? = @connected
    def reconnecting? = @reconnecting_now
    def lost_reason = @lost_now
    def count(kind) = @calls.count { |c| c[0] == kind }
  end
end

Settings = Struct.new(:node, :router, :credentials, :error)

load LINK_FILE

class Owner
  attr_reader :ups, :downs
  attr_accessor :raise_in_up

  def initialize
    @ups = []
    @downs = []
  end

  def link_up(link)
    raise Asterism::Error, "expose failed" if @raise_in_up
    @ups << [link.state, link.connected?]
  end

  def link_down(link)
    @downs << [link.state, link.reason]
  end
end

# An owner without link_down: Link must not call it.
class UpOnly
  attr_reader :ups

  def initialize
    @ups = 0
  end

  def link_up(_link)
    @ups += 1
  end
end

def fresh(router: "tcp/192.0.2.2:7447", error: nil, wifi: nil, **opts)
  Asterism.reset
  Log.lines.clear
  Machine.ms = 1000
  FmrbApp.wifi = wifi
  owner = opts.delete(:owner) || Owner.new
  link = FmrbAsterism::Link.new(owner, "step", tag: "t", settings: Settings.new("nodeA", router, :creds, error), **opts)
  [link, owner]
end

puts "a machine without WiFi connects at once (one update in :connecting first)"
link, owner = fresh
check("before the first update", link.state, :waiting_wifi)
check("first update returns false", link.update, false)
check("first update: connecting, nothing blocking yet", [link.state, Asterism.count(:connect)], [:connecting, 0])
check("second update connects and returns true", link.update, true)
check("connected", [link.state, link.connected?, link.up?, link.reason], [:connected, true, true, ""])
check("connect got the settings", Asterism.calls.find { |c| c[0] == :connect },
      [:connect, "tcp/192.0.2.2:7447", "nodeA", "step", nil, nil, :creds])
check("link_up ran once, seeing :connected", owner.ups, [[:connected, true]])
check("connects counted", link.connects, 1)
check("logged", Log.lines.last, "I t: nodeA/step connected (tcp/192.0.2.2:7447, 0 ms)")
check("node and router readable", [link.node, link.router], ["nodeA", "tcp/192.0.2.2:7447"])
link.update
check("the next update polls", Asterism.count(:poll), 1)

puts "WiFi without an address"
link, owner = fresh(wifi: { connected: false })
link.update
check("waits for WiFi", link.state, :waiting_wifi)
Machine.advance(19_000)
link.update
check("still waiting at 19 s", link.state, :waiting_wifi)
FmrbApp.wifi = { connected: true }
link.update
check("WiFi up: connecting", link.state, :connecting)
link, owner = fresh(wifi: { connected: false }, wifi_wait_ms: 5000)
Machine.advance(5001)
link.update
check("after wifi_wait_ms it tries anyway", link.state, :connecting)

puts "waiting for the screen"
link, owner = fresh(screen_wait_ms: 2000)
link.update
check("waits for the screen", link.state, :waiting_screen)
link.screen_ready
link.update
check("screen_ready: connecting", link.state, :connecting)
link, owner = fresh(screen_wait_ms: 2000)
link.update
Machine.advance(2001)
link.update
check("after screen_wait_ms it connects anyway", link.state, :connecting)
link, owner = fresh(wifi: { connected: false }, screen_wait_ms: 2000)
Machine.advance(3000)
link.update
check("WiFi is waited for first", link.state, :waiting_wifi)

puts "a connect that fails"
link, owner = fresh
Asterism.fail = [Asterism::Disconnected.new("no router")]
link.update
link.update
check("retrying, with the reason", [link.state, link.reason, link.connected?], [:retrying, "no router", false])
check("closed after the failure", Asterism.calls.last, [:close])
check("link_down told", owner.downs, [[:retrying, "no router"]])
check("logged as a warning", Log.lines.last, "W t: connect: no router")
check("link_up not run", owner.ups, [])
Machine.advance(2999)
link.update
check("not again before retry_ms", [link.state, Asterism.count(:connect)], [:retrying, 1])
Machine.advance(1)
link.update
check("at retry_ms: connecting", link.state, :connecting)
link.update
check("then connected", [link.state, owner.ups.size, link.connects], [:connected, 1, 1])

puts "the previous run still on the network"
link, owner = fresh
Asterism.fail = [Asterism::Error.new("nodeA/step is already on the network")]
link.update
link.update
check("still_there", link.state, :still_there)
check("the reason is kept", link.reason, "nodeA/step is already on the network")
Machine.advance(3000)
link.update
link.update
check("connects after waiting", link.state, :connected)

puts "an error that is not Asterism's"
link, owner = fresh
Asterism.fail = [RuntimeError.new("socket")]
link.update
link.update
check("named with its class", [link.state, link.reason], [:retrying, "RuntimeError: socket"])

puts "the settings in error (no credentials for tls/)"
link, owner = fresh(error: "tls/x needs a credentials .tar")
link.update
link.update
check("retrying with the settings' error", [link.state, link.reason], [:retrying, "tls/x needs a credentials .tar"])
check("Asterism.connect never called", Asterism.count(:connect), 0)

puts "link_up raises (expose failed)"
link, owner = fresh
owner.raise_in_up = true
link.update
link.update
check("counts as a failed connect", [link.state, link.reason], [:retrying, "expose failed"])
check("the half-made connection is closed", Asterism.connected?, false)
check("not counted", link.connects, 0)
owner.raise_in_up = false
Machine.advance(3000)
link.update
link.update
check("next try works", [link.state, link.connects], [:connected, 1])

puts "a lost connection"
link, owner = fresh
link.update
link.update
Asterism.lost_now = "the connection was lost"
Asterism.poll_plan = [false]
check("update returns false", link.update, false)
check("lost, with Asterism's reason", [link.state, link.reason], [:lost, "asterism: the connection was lost"])
check("closed", Asterism.calls.last, [:close])
check("link_down told", owner.downs, [[:lost, "asterism: the connection was lost"]])
check("logged", Log.lines.last, "W t: lost: asterism: the connection was lost")
Machine.advance(3000)
link.update
check("connecting again after retry_ms", link.state, :connecting)
link.update
check("connected again", link.state, :connected)
check("link_up ran again (expose and on_join after every connect)", owner.ups.size, 2)
check("two connects", link.connects, 2)

puts "poll raises"
link, owner = fresh
link.update
link.update
Asterism.poll_plan = [RuntimeError.new("bad block")]
link.update
check("lost with the exception", [link.state, link.reason], [:lost, "RuntimeError: bad block"])

puts "Zenoh reopening the session by itself"
link, owner = fresh
link.update
link.update
Asterism.reconnecting_now = true
check("update still returns true", link.update, true)
check("reconnecting: connected? but not up?", [link.state, link.connected?, link.up?], [:reconnecting, true, false])
Asterism.reconnecting_now = false
link.update
check("back to connected", link.state, :connected)
check("no new connect", Asterism.count(:connect), 1)

puts "the application gives the connection up (Link#lost)"
link, owner = fresh
link.update
link.update
link.lost("RuntimeError: in each_pending")
check("lost with the application's reason", [link.state, link.reason], [:lost, "RuntimeError: in each_pending"])
check("closed", Asterism.connected?, false)

puts "no retries (retry_ms: nil)"
link, owner = fresh(retry_ms: nil)
Asterism.fail = [Asterism::Disconnected.new("no router")]
link.update
link.update
check("failed", [link.state, link.reason], [:failed, "no router"])
Machine.advance(100_000)
link.update
check("stays failed", [link.state, Asterism.count(:connect)], [:failed, 1])
link, owner = fresh(retry_ms: nil)
link.update
link.update
Asterism.lost_now = "gone"
Asterism.poll_plan = [false]
link.update
Machine.advance(100_000)
link.update
check("a loss stays lost", [link.state, Asterism.count(:connect)], [:lost, 1])

puts "peer locators (asterism_demo's listen / peer)"
link, owner = fresh(router: "listen tcp/0.0.0.0:7447")
link.update
link.update
check("listen: no locator, peer mode", Asterism.calls.find { |c| c[0] == :connect },
      [:connect, nil, "nodeA", "step", :peer, "tcp/0.0.0.0:7447", nil])
link, owner = fresh(router: "peer tcp/192.0.2.3:7447")
link.update
link.update
check("peer: the locator, peer mode", Asterism.calls.find { |c| c[0] == :connect },
      [:connect, "tcp/192.0.2.3:7447", "nodeA", "step", :peer, nil, nil])

puts "an owner without link_down"
link, owner = fresh(owner: UpOnly.new)
Asterism.fail = [Asterism::Disconnected.new("no router")]
link.update
link.update
check("a failure is fine without it", link.state, :retrying)
Machine.advance(3000)
link.update
link.update
check("link_up still runs", owner.ups, 1)

puts "close"
link, owner = fresh
link.update
link.update
link.close
check("closed", [link.state, Asterism.connected?, link.connected?], [:closed, false, false])
check("update does nothing more", [link.update, Asterism.count(:connect)], [false, 1])

puts
if Check.failed > 0
  puts "#{Check.failed} FAILED"
  exit 1
end
puts "all passed"
