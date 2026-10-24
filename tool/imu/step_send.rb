#!/usr/bin/env ruby
# Puts steps on Asterism the way the step sensor app does
# (flash/app/test/step_sensor.app.rb), for trying a reader without walking
# with a board: asterism/<node>/step/pedometer/step, a MessagePack map with
# steps, t_ms, interval_ms, cadence, ago_ms, sent_ms, event, sensor, raw.
#
#   ruby tool/imu/step_send.rb [--count N] [--cadence SPM] [--start N]
#        [--node NAME] [--router tcp/127.0.0.1:7447] [--interactive]
#        [--measure] [--wait SEC]
#
# --count N      steps to send (default 10), at --cadence steps per minute
#                (default 110). "--cadence 90:20,140:20" walks 20 steps at
#                90, then 20 at 140 (the counts replace --count).
# --start N      the total before the first step (default 0)
# --node NAME    the node ID to put under (default pc-stepsend)
# --interactive  one step for each Enter, q (or end of input) to stop
# --measure      also listens to asterism/*/walk/scroller/progress (what
#                flash/app/demo/walk_scroller.app.rb puts after the frame
#                that shows a step) and prints the time from each put here
#                to that frame being presented
# --wait SEC     before the first step, the "state" value and a pause
#                (default 1.5 s) so a reader learns the starting total
#
# Every 3 s without a step it repeats the last value (event "repeat"), as
# the app does. Needs the asterism gem.
require "asterism"
require "msgpack"

$stdout.sync = true

router = "tcp/127.0.0.1:7447"
node = "pc-stepsend"
count = 10
cadence = 110
plan = nil
start = 0
interactive = false
measure = false
wait = 1.5
args = ARGV.dup
until args.empty?
  a = args.shift
  case a
  when "--router" then router = args.shift
  when "--node" then node = args.shift
  when "--count" then count = args.shift.to_i
  when "--cadence"
    v = args.shift
    if v.include?(":")
      plan = v.split(",").map { |seg| c, n = seg.split(":"); [c.to_f, n.to_i] }
    else
      cadence = v.to_f
    end
  when "--start" then start = args.shift.to_i
  when "--interactive" then interactive = true
  when "--measure" then measure = true
  when "--wait" then wait = args.shift.to_f
  when "-h", "--help"
    puts File.read(__FILE__)[/\A.*?\n\n/m].lines.drop(1).map { _1.sub(/^# ?/, "") }.join
    exit
  else abort "unknown argument #{a} (--help)"
  end
end
plan ||= [[cadence, count]]

def now_ms
  Process.clock_gettime(Process::CLOCK_MONOTONIC, :float_millisecond)
end

T0 = now_ms

def clock
  (now_ms - T0).round
end

class Walker
  attr_reader :steps

  def initialize(start)
    @steps = start
    @last = nil
    @intervals = []
  end

  def step
    t = clock
    iv = 0
    if @last
      gap = t - @last
      if gap <= 2000
        iv = gap
        @intervals << gap
        @intervals.shift while @intervals.size > 4
      else
        @intervals = []
      end
    end
    @last = t
    @steps += 1
    value("step", iv)
  end

  def cadence
    return 0 if @intervals.empty? || @last.nil? || clock - @last > 2000
    (60_000.0 * @intervals.size / @intervals.sum).round
  end

  def value(event, iv = 0)
    t = clock
    { "steps" => @steps, "t_ms" => @last, "interval_ms" => iv, "cadence" => cadence,
      "ago_ms" => @last ? t - @last : 0, "sent_ms" => t, "event" => event, "sensor" => "ok", "raw" => false }
  end
end

key = "asterism/#{node}/step/pedometer/step"
puts_at = {}
lat = []
lock = Mutex.new

Asterism.connect(router, node: node, app: "step") do |net|
  if measure
    net.session.subscribe("asterism/*/walk/scroller/progress") do |sm|
      here = now_ms
      v = MessagePack.unpack(sm.payload)
      next unless v["src"] == node
      t = lock.synchronize { puts_at.delete(v["src_steps"]) }
      next unless t
      d = here - t
      lock.synchronize { lat << d }
      who = sm.key.to_s.split("/")[1]
      puts format("  shown on %s: steps %d -> count %d, mark %d, %.0f ms after the put",
                  who, v["src_steps"], v["count"], v["mark"], d)
    end
  end
  net.start
  w = Walker.new(start)
  put = lambda do |v|
    lock.synchronize { puts_at[v["steps"]] = now_ms } if v["event"] == "step"
    net.session.put(key, MessagePack.pack(v))
  end
  last_put = now_ms
  put.call(w.value("state"))
  puts "#{key}: state, steps #{w.steps}"
  sleep wait

  if interactive
    puts "Enter: one step, q: stop"
    q = Queue.new
    Thread.new { $stdin.each_line { |l| q << l }; q << :eof }
    loop do
      l = q.pop(timeout: 3)
      if l.nil? # 3 s without a step
        put.call(w.value("repeat"))
        last_put = now_ms
        next
      end
      break if l == :eof || l.strip == "q"
      v = w.step
      put.call(v)
      last_put = now_ms
      puts format("%s step %d (interval %d ms, cadence %d)", Time.now.strftime("%H:%M:%S.%L"), v["steps"],
                  v["interval_ms"], v["cadence"])
    end
  else
    plan.each do |spm, n|
      gap = 60.0 / spm
      n.times do
        t = now_ms
        v = w.step
        put.call(v)
        puts format("%s step %d (interval %d ms, cadence %d)", Time.now.strftime("%H:%M:%S.%L"), v["steps"],
                    v["interval_ms"], v["cadence"])
        rest = gap - (now_ms - t) / 1000.0
        sleep rest if rest > 0
      end
    end
  end
  sleep 1.0
  put.call(w.value("repeat"))
  sleep 0.5 if measure
  if measure
    s = lat.sort
    if s.empty?
      puts "no progress values came back (is walk_scroller running and connected?)"
    else
      puts format("put to presented: n=%d min %.0f / median %.0f / max %.0f ms", s.size, s.first, s[s.size / 2], s.last)
    end
  end
end
