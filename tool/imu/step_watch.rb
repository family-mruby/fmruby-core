#!/usr/bin/env ruby
# Watches the steps a board's step sensor app (flash/app/test/step_sensor.app.rb)
# puts on Asterism, and how long each one took to arrive here.
#
#   ruby tool/imu/step_watch.rb <node> [--router tcp/127.0.0.1:7447]
#        [--reset] [--raw on|off] [--accel] [--duration SEC] [--quiet]
#
# <node> is the board's node ID (fmruby-XXXXXX; `asterism node list`).
# --reset calls pedometer.reset first, --raw switches the raw values,
# --accel also prints the raw values (20 Hz). Needs the asterism gem.
#
# The delay: the board's value carries t_ms, its own clock at the step.
# The board's clock is related to this machine's by calling pedometer.clock
# twice a second (the board read its clock within the call, so each call
# places it within half its round trip) and fitting a line through the
# calls of the last 10 s with the shortest round trips (rate and offset:
# this machine's clocks are not steady enough for a single offset, below).
# Each step's delay is the time it arrived here minus t_ms on this
# machine's clock: detection, the put and the network, from the sample that
# crossed the threshold to here.
require "asterism"
require "msgpack"

$stdout.sync = true

node = nil
router = "tcp/127.0.0.1:7447"
reset = false
raw = nil
accel = false
duration = nil
quiet = false
args = ARGV.dup
until args.empty?
  a = args.shift
  case a
  when "--router" then router = args.shift
  when "--reset" then reset = true
  when "--raw" then raw = args.shift == "on"
  when "--accel" then accel = true
  when "--duration" then duration = args.shift.to_f
  when "--quiet" then quiet = true
  when "-h", "--help"
    puts File.read(__FILE__)[/\A.*?\n\n/m].lines.drop(1).map { _1.sub(/^# ?/, "") }.join
    exit
  else node = a
  end
end
abort "usage: step_watch.rb <node> [--router LOC] [--reset] [--raw on|off] [--accel] [--duration SEC]" unless node

# Under WSL2 the monotonic clock was measured running 1% slow against the
# board's clock (61.2 s of the board's in 60.6 s of it), and the wall clock
# is stepped back into line every 20 s or so (a saw of up to 300 ms). The
# monotonic clock with a fitted rate (Sync) copes with both.
def now_ms
  Process.clock_gettime(Process::CLOCK_MONOTONIC, :float_millisecond)
end

# The board's clock against this machine's: calls of pedometer.clock, a
# line through the recent ones with the shortest round trips.
class Sync
  KEEP_MS = 10_000

  def initialize(ped)
    @ped = ped
    @calls = [] # [here (middle of the call), board, round trip]
    @lock = Mutex.new
  end

  def call_once
    t0 = now_ms
    b = @ped.clock
    t1 = now_ms
    @lock.synchronize do
      @calls << [(t0 + t1) / 2, b, t1 - t0]
      @calls.shift while @calls.size > 2 && @calls.last[0] - @calls.first[0] > KEEP_MS
    end
    t1 - t0
  end

  def stop
    @thread&.kill
    @thread&.join
  end

  def start
    @thread = Thread.new do
      loop do
        begin
          call_once
        rescue StandardError
          nil
        end
        sleep 0.5
      end
    end
  end

  def min_rtt
    @lock.synchronize { @calls.map { _1[2] }.min }
  end

  # This machine's time (ms) for the board's time b.
  def here_of(b)
    pts = @lock.synchronize do
      best = @calls.map { _1[2] }.min
      @calls.select { _1[2] <= best + 8 }
    end
    return nil if pts.empty?
    if pts.size < 2 || pts.last[0] - pts.first[0] < 2000
      p = pts.min_by { _1[2] }
      return p[0] + (b - p[1])
    end
    # board = a * here + c
    n = pts.size.to_f
    mx = pts.sum { _1[0] } / n
    my = pts.sum { _1[1] } / n
    sxx = pts.sum { (_1[0] - mx)**2 }
    sxy = pts.sum { (_1[0] - mx) * (_1[1] - my) }
    a = sxy / sxx
    mx + (b - my) / a
  end
end

def stats(a)
  return "-" if a.empty?
  s = a.sort
  med = s[s.size / 2]
  format("n=%d min %.0f / median %.0f / max %.0f ms", s.size, s.first, med, s.last)
end

Asterism.connect(router, node: "pc-stepwatch-#{Process.pid}", app: "stepwatch") do |net|
  ped = net["#{node}/step/pedometer"]
  sync = Sync.new(ped)
  10.times { sync.call_once }
  rtt = sync.min_rtt
  puts format("clock: shortest round trip %.0f ms (each call places the board's clock within +-%.0f ms)",
              rtt, rtt / 2)
  sync.start
  if reset
    t0 = now_ms
    st = ped.reset
    puts format("reset -> steps %d (%.0f ms)", st["steps"], now_ms - t0)
  end
  unless raw.nil?
    puts "raw -> #{ped.raw(raw)}"
  end
  delays = []
  put_delays = []
  last_steps = nil
  key = "asterism/#{node}/step/pedometer/step"
  net.session.subscribe(key) do |sm|
    here = now_ms
    v = MessagePack.unpack(sm.payload)
    if v["event"] == "step" && v["t_ms"]
      d = here - sync.here_of(v["t_ms"])
      put = here - sync.here_of(v["sent_ms"])
      delays << d
      puts format("%s step %5d  cadence %3d/min  interval %4d ms  delay %4.0f ms (put to here %3.0f ms)",
                  Time.now.strftime("%H:%M:%S.%L"), v["steps"], v["cadence"], v["interval_ms"], d, put)
    else
      put = here - sync.here_of(v["sent_ms"])
      put_delays << put
      if !quiet || v["steps"] != last_steps
        puts format("%s %-6s steps %5d  cadence %3d/min  sensor %s  raw %s  (put to here %3.0f ms)",
                    Time.now.strftime("%H:%M:%S.%L"), v["event"], v["steps"], v["cadence"].to_i, v["sensor"],
                    v["raw"], put)
      end
    end
    last_steps = v["steps"]
  end
  if accel
    net.session.subscribe("asterism/#{node}/step/pedometer/accel") do |sm|
      v = MessagePack.unpack(sm.payload)
      puts format("accel x %6d y %6d z %6d  m %5d peak %5d dyn %5d (rise %d) mg", v["x"], v["y"], v["z"],
                  v["m"], v["m_peak"], v["dyn"], v["rise"])
    end
  end
  net.start # the receiving thread (the subscriptions above)
  puts "watching #{key} (Ctrl-C to stop)"
  t_end = duration ? now_ms + duration * 1000 : nil
  begin
    loop do
      sleep 0.2
      break if t_end && now_ms > t_end
    end
  rescue Interrupt
    nil
  end
  sync.stop
  puts "step delays: #{stats(delays)}"
  puts "other values, put to here: #{stats(put_delays)}"
end
