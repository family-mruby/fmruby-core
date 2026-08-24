#!/usr/bin/env ruby
# Host tests for the service host's rules
# (main/prebuild_scripts/default_app/services/registry.rb).
#
# The host itself talks to files, Pub/Sub and the clock and can only be
# checked in the simulator. What decides its *behaviour* does not: the toml
# reader, the two-layer merge, the error budget and the sleep calculation are
# plain Ruby over Strings and Hashes, so the real file runs here unchanged.
# Same tier and same reasoning as test/fmrb_ui/run.rb -- no docker, no
# firmware, no device.
#
# What this cannot see: whether a service file actually loads on the device,
# whether the subscriptions arrive, and what a handler costs. Those stay with
# the simulator (doc/user_extension/services/report/s1.md).
#
#   ruby test/services/run.rb        (or: rake services:test)

load File.expand_path("../../main/prebuild_scripts/default_app/services/registry.rb", __dir__)

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

# --- the toml subset ------------------------------------------------------

SAMPLE = <<~TOML
  # a comment line
  [heartbeat]
  file = "heartbeat.rb"
  class = "HeartbeatService"
  enable = true
  interval_ms = 10000

  [chime]
  file = "chime.rb"
  oneshot = false
  delay_ms = -5          # negatives parse as integers

  [chime.config]
  hour_only = true
  label = "top of the hour"
  volume = 8
TOML

conf = SvcConf.parse(SAMPLE)
check("both tables", conf.keys, ["heartbeat", "chime"])
check("string value loses its quotes", conf["heartbeat"]["file"], "heartbeat.rb")
check("true is a boolean", conf["heartbeat"]["enable"], true)
check("integer is an Integer", conf["heartbeat"]["interval_ms"], 10000)
check("false is a boolean", conf["chime"]["oneshot"], false)
check("trailing comment is not part of the value", conf["chime"]["delay_ms"], -5)
check("sub-table lands in config", conf["chime"]["config"]["hour_only"], true)
check("a quoted value may contain a #", conf["chime"]["config"]["label"], "top of the hour")
check("and its neighbours still parse", conf["chime"]["config"]["volume"], 8)
check("config is not a plain field", conf["heartbeat"]["config"], nil)

# A hand-written list has typos in it. They must cost their own line and
# nothing else -- the whole point of parsing rather than eval'ing.
BROKEN = <<~TOML
  [good
  file = "orphan.rb"
  [ok]
  file = "ok.rb"
  no equals sign here
  = 5
  interval_ms = 100
  [ok.other]
  ignored = 1
  [ok.config]
  n = 1
TOML
conf = SvcConf.parse(BROKEN)
check("an unterminated table is skipped", conf.keys, ["ok"])
check("and so are the lines under it", conf["ok"]["file"], "ok.rb")
check("junk lines do not stop the table", conf["ok"]["interval_ms"], 100)
check("an unknown sub-table is dropped", conf["ok"]["other"], nil)
check("but config after it still lands", conf["ok"]["config"]["n"], 1)
check("empty text is an empty list", SvcConf.parse(nil).keys, [])

check("bare words stay strings", SvcConf.parse_value("hello"), "hello")
check("so does an empty value", SvcConf.parse_value(""), "")
check("not-a-number does not become 0", SvcConf.parse_value("12ab"), "12ab")

# --- the two layers -------------------------------------------------------

sys = SvcConf.parse(<<~TOML)
  [clock]
  file = "clock.rb"
  class = "ClockService"
  interval_ms = 60000
  [clock.config]
  quiet_hours = 0
  tone = "bell"
TOML
usr = SvcConf.parse(<<~TOML)
  [clock]
  enable = false
  [clock.config]
  tone = "chime"
  [heartbeat]
  file = "heartbeat.rb"
TOML

m = SvcConf.merge(sys, usr)
check("system entries come first", m.keys, ["clock", "heartbeat"])
check("a user field wins", m["clock"]["enable"], false)
check("fields it did not name survive", m["clock"]["interval_ms"], 60000)
check("the origin stays with the layer that introduced it", m["clock"]["origin"], "sys")
check("a user-only service is usr", m["heartbeat"]["origin"], "usr")
check("config merges key by key", m["clock"]["config"]["tone"], "chime")
check("and keeps the rest", m["clock"]["config"]["quiet_hours"], 0)
check("the system list is not modified", sys["clock"]["config"]["tone"], "bell")
check("nor is a field of it", sys["clock"].key?("origin"), false)

# --- one entry ------------------------------------------------------------

e = SvcEntry.new("clock", m["clock"])
check("enable = false is respected", e.enabled?, false)
check("and shows as stopped", e.state, "stopped")

e = SvcEntry.new("hb", { "file" => "hb.rb", "class" => "H", "interval_ms" => 100,
                         "origin" => "usr" })
check("a listed service runs", e.state, "running")
check("no config is an empty Hash", e.config, {})
check("not an app entry", e.app?, false)
e.arm(1000)
check("armed to now + interval", e.next_at, 1100)
check("not due before", e.due?(1099), false)
check("due at the deadline", e.due?(1100), true)

# A subscribe-only service has no deadline at all.
sub = SvcEntry.new("sub", { "file" => "s.rb", "class" => "S" })
sub.arm(1000)
check("no interval means no deadline", sub.next_at, nil)
check("and it is never due", sub.due?(999999), false)

app = SvcEntry.new("game", { "app" => "/app/game/x.app.rb", "fullscreen" => true,
                             "delay_ms" => 2000 })
check("an app entry knows it", app.app?, true)
check("fullscreen is a boolean", app.fullscreen, true)
app.arm_app(500)
check("armed to now + delay", app.next_at, 2500)

# --- the error budget -----------------------------------------------------
#
# One error must not switch a service off: a job that loses a race against
# some hardware during boot works for the rest of the session. Three is the
# line, and it is reported exactly once.

e = SvcEntry.new("bad", { "file" => "b.rb", "class" => "B", "interval_ms" => 10 })
check("the first error is survivable", e.note_error, false)
check("so is the second", e.note_error, false)
check("still running", e.running?, true)
check("the third is the last", e.note_error, true)
check("now failed", e.state, "failed")
check("and out of the delivery list", e.deliverable?, false)
check("the count is not reported twice", e.note_error, false)
check("errors keep counting for ps", e.errors, 4)

check("start clears the count", e.start(500), true)
check("and the state", e.state, "running")
check("from zero", e.errors, 0)
check("re-armed", e.next_at, 510)
check("starting a running service is refused", e.start(500), false)

check("stop takes it out", e.stop, true)
check("with no deadline", e.next_at, nil)
check("stopping twice is refused", e.stop, false)

e2 = SvcEntry.new("c", { "file" => "c.rb", "class" => "C" })
e2.obj = Object.new
check("a loaded running service is deliverable", e2.deliverable?, true)
e2.stop
check("a stopped one is not", e2.deliverable?, false)

# --- counters and the report ----------------------------------------------

e3 = SvcEntry.new("r", { "file" => "r.rb", "class" => "R", "origin" => "sys" })
e3.note_tick
e3.note_tick
e3.note_event
e3.note_error
check("report", e3.to_report,
      { "name" => "r", "origin" => "sys", "state" => "running",
        "ticks" => 2, "events" => 1, "wakes" => 0, "errors" => 1 })

# --- wake_in --------------------------------------------------------------
#
# A one-off callback, kept in its own slot. The two rules that matter: asking
# again replaces the pending one rather than queueing a second, and it never
# moves the periodic deadline -- a service that asks for a wake must not
# quietly change its own tick rate.

w = SvcEntry.new("w", { "file" => "w.rb", "class" => "W", "interval_ms" => 1000 })
w.arm(0)
check("no wake to begin with", w.wake_at, nil)
check("and none is due", w.wake_due?(999999), false)
w.wake_in(100, 200)
check("armed from the moment it asked", w.wake_at, 300)
check("the tick deadline did not move", w.next_at, 1000)
check("not due before", w.wake_due?(299), false)
check("due at the deadline", w.wake_due?(300), true)
check("taking it reports the take", w.take_wake, true)
check("and clears it", w.wake_at, nil)
check("so it cannot fire twice", w.take_wake, false)

w.wake_in(400, 50)
w.wake_in(410, 500)
check("asking again replaces, never queues", w.wake_at, 910)
check("still one deadline after the replacement", w.take_wake, true)
check("and nothing behind it", w.take_wake, false)

w.wake_in(0, -5)
check("a negative delay is now, not the past", w.wake_at, 0)
w.wake_in(0, 10)
w.stop
check("stopping drops the pending wake", w.wake_at, nil)
check("and a stopped service is never wake-due", w.wake_due?(999999), false)

w2 = SvcEntry.new("w2", { "file" => "w.rb", "class" => "W" })
w2.wake_in(0, 10)
w2.note_error
w2.note_error
w2.note_error
check("failing for good drops it too", w2.wake_at, nil)

w3 = SvcEntry.new("w3", { "file" => "w.rb", "class" => "W" })
w3.note_wake
check("wakes are counted for ps", w3.to_report["wakes"], 1)

# --- how long the host may sleep ------------------------------------------

check("nothing to run means nothing to wake for", SvcConf.next_sleep([], 0),
      SvcConf::IDLE_SLEEP_MS)

a = SvcEntry.new("a", { "file" => "a.rb", "class" => "A", "interval_ms" => 10000 })
b = SvcEntry.new("b", { "file" => "b.rb", "class" => "B", "interval_ms" => 300 })
a.arm(0)
b.arm(0)
check("the nearest deadline wins", SvcConf.next_sleep([a, b], 0), 300)
check("floored, never zero", SvcConf.next_sleep([a, b], 299), SvcConf::MIN_SLEEP_MS)
check("overdue is the floor too", SvcConf.next_sleep([a, b], 5000), SvcConf::MIN_SLEEP_MS)
# No ceiling: a slow service is slept out in one go rather than polled at.
check("a far deadline is slept to in one go", SvcConf.next_sleep([a], 0), 10000)
b.stop
check("a stopped service sets no deadline", SvcConf.next_sleep([a, b], 0), 10000)
check("subscribe-only services do not either", SvcConf.next_sleep([sub], 0),
      SvcConf::IDLE_SLEEP_MS)

# A wake counts as a deadline, and beats a later tick.
c = SvcEntry.new("c2", { "file" => "c.rb", "class" => "C", "interval_ms" => 10000 })
c.arm(0)
c.wake_in(0, 120)
check("the wake is what the host wakes for", SvcConf.next_sleep([c], 0), 120)
c.take_wake
check("and the tick deadline is back once it is taken",
      SvcConf.next_sleep([c], 0), 10000)
sub.wake_in(0, 700)
check("even a subscribe-only service can have one",
      SvcConf.next_sleep([sub], 0), 700)
sub.take_wake

puts(Check.failed.zero? ? "services: all checks passed" : "services: #{Check.failed} FAILED")
exit(Check.failed.zero? ? 0 : 1)
