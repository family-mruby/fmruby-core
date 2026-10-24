# Checks the step detector of flash/app/test/step_sensor.app.rb against
# synthetic accelerometer data: walking at 1.5 to 2.5 steps per second,
# lying still, noise, slow tilting, and a walk with a pause. Prints the count
# and the delay of each case and exits non-zero when a case is off.
#
#   ruby tool/imu/test/step_detector_test.rb [-v]
#
# The detector is the part of the app file between the StepDetector markers,
# loaded as it is: the same code runs on the board.
#
# The model: gravity (1 g) in a random fixed direction; along it, each step
# is an impact (a Gaussian pulse, peak at the step's time) on top of the
# body's up and down (a sine at the step rate); across it, the sway (half
# the step rate); plus white noise. The sensor makes a sample every 10 ms;
# the app reads the FIFO every POLL ms (with jitter and stalls), so a
# sample can be read late, as on the board. The delay is from the impact's
# peak to the read that reports the step; the impact starts rising about
# 2 widths (50 ms) before its peak.

APP = File.expand_path("../../../flash/app/test/step_sensor.app.rb", __dir__)
src = File.read(APP)
part = src[/^# --- StepDetector \(.*?^# --- StepDetector end ---$/m]
abort "StepDetector markers not found in #{APP}" unless part
eval(part, TOPLEVEL_BINDING, APP)

VERBOSE = ARGV.include?("-v")

class Walk
  ODR_MS = 10

  def initialize(seed)
    @rng = Random.new(seed)
  end

  def gauss
    # Box-Muller
    u1 = 1.0 - @rng.rand
    u2 = @rng.rand
    Math.sqrt(-2.0 * Math.log(u1)) * Math.cos(2 * Math::PI * u2)
  end

  def unit_vector
    loop do
      v = [gauss, gauss, gauss]
      n = Math.sqrt(v.sum { |e| e * e })
      return v.map { |e| e / n } if n > 1e-6
    end
  end

  def perpendicular(v)
    a = v[0].abs < 0.9 ? [1.0, 0.0, 0.0] : [0.0, 1.0, 0.0]
    c = [v[1] * a[2] - v[2] * a[1], v[2] * a[0] - v[0] * a[2], v[0] * a[1] - v[1] * a[0]]
    n = Math.sqrt(c.sum { |e| e * e })
    c.map { |e| e / n }
  end

  # segments: [[:walk, seconds, rate], [:still, seconds], ...]
  # opts: impact (g), width_ms, bounce (g), noise (g), jitter (fraction of
  # the interval), tilt (radians over the whole run, while still)
  # Returns [samples [[t, x, y, z]], step times (impact peaks)]
  def generate(segments, impact: 0.35, width_ms: 25.0, bounce: 0.12, sway: 0.05, noise: 0.01,
               jitter: 0.08, amp_var: 0.2, tilt: 0.0)
    g = unit_vector
    side = perpendicular(g)
    steps = []
    plan = [] # [start_ms, end_ms, rate or nil]
    t = 1000 # a second of rest first: the slow average settles
    plan << [0, t, nil]
    segments.each do |kind, secs, rate|
      plan << [t, t + (secs * 1000).round, kind == :walk ? rate : nil]
      t += (secs * 1000).round
    end
    plan << [t, t + 1000, nil]
    total = t + 1000
    amps = {}
    plan.each do |s, e, rate|
      next unless rate
      period = 1000.0 / rate
      tp = s + period / 2
      while tp < e
        steps << tp
        amps[tp] = impact * (1.0 + amp_var * (@rng.rand * 2 - 1))
        tp += period * (1.0 + jitter * (@rng.rand * 2 - 1))
      end
    end
    samples = []
    ts = 0
    while ts <= total
      walking = plan.find { |s, e, r| r && ts >= s && ts < e }
      vert = 0.0
      across = 0.0
      if walking
        # The body's up and down follows the steps: highest at the impact,
        # lowest half a step away (the phase of the nearest step).
        period = 1000.0 / walking[2]
        near = steps.min_by { |tp| (tp - ts).abs }
        phase = 2 * Math::PI * (ts - near) / period
        vert += bounce * Math.cos(phase)
        across += sway * Math.sin(Math::PI * (ts - walking[0]) / period)
      end
      steps.each do |tp|
        d = (ts - tp) / width_ms
        next if d.abs > 4
        vert += amps[tp] * Math.exp(-d * d)
      end
      # Slow tilt (only meaningful while still): rotate g toward side.
      ang = tilt * ts / total
      gd = [0, 1, 2].map { |i| g[i] * Math.cos(ang) + side[i] * Math.sin(ang) }
      sd = [0, 1, 2].map { |i| side[i] * Math.cos(ang) - g[i] * Math.sin(ang) }
      a = [0, 1, 2].map { |i| gd[i] * (1.0 + vert) + sd[i] * across + noise * gauss }
      samples << [ts, *a]
      ts += ODR_MS
    end
    [samples, steps]
  end

  # The app's reads: every poll_ms (+ jitter, and a stall of stall_ms every
  # stall_every reads: a screen redraw, a put, a call answered), all the
  # samples made since the last read, from the FIFO. As in the app, the
  # newest sample is taken as made at the read and the earlier ones 10 ms
  # apart before it. Returns the read times that reported a step.
  def run(det, samples, poll_ms: 10, poll_jitter: 6, read_ms: 2, stall_every: 40, stall_ms: 60)
    out = []
    t = 0.0
    next_index = 0
    reads = 0
    while t <= samples.last[0]
      last = (t / ODR_MS).floor
      last = samples.size - 1 if last >= samples.size
      if last >= next_index
        now = t.round + read_ms
        n = last - next_index + 1
        step = false
        (next_index..last).each_with_index do |i, k|
          s = samples[i]
          step = true if det.feed(now - (n - 1 - k) * ODR_MS, s[1], s[2], s[3])
        end
        out << now if step
        next_index = last + 1
      end
      reads += 1
      t += poll_ms + @rng.rand * poll_jitter
      t += stall_ms if stall_every > 0 && reads % stall_every == 0
    end
    out
  end
end

# Matches detections to the true steps (each detection to the nearest step
# within 300 ms that is not taken yet). Returns [matched delays, extra, missed].
def match(detections, steps)
  free = steps.dup
  delays = []
  extra = 0
  detections.each do |d|
    best = free.min_by { |s| (s - d).abs }
    if best && (d - best).between?(-150, 300)
      delays << d - best
      free.delete(best)
    else
      extra += 1
    end
  end
  [delays, extra, free.size]
end

def pct(a, q)
  return 0 if a.empty?
  s = a.sort
  s[[(q * (s.size - 1)).round, s.size - 1].min]
end

CASES = [
  # name, segments, generate options, run options, allowed miss, allowed extra
  ["walk 1.5 steps/s", [[:walk, 40, 1.5]], {}, {}, 1, 0],
  ["walk 2.0 steps/s", [[:walk, 40, 2.0]], {}, {}, 1, 0],
  ["walk 2.5 steps/s", [[:walk, 40, 2.5]], {}, {}, 1, 0],
  ["walk 2.0, light (impact 0.22 g)", [[:walk, 40, 2.0]], { impact: 0.22, bounce: 0.08 }, {}, 2, 0],
  ["walk 2.0, noisy (0.03 g)", [[:walk, 40, 2.0]], { noise: 0.03 }, {}, 1, 1],
  ["walk 2.0, slow polling (20-30 ms)", [[:walk, 40, 2.0]], {}, { poll_ms: 20, poll_jitter: 10 }, 1, 0],
  ["walk 2.0, sharp impacts (width 12 ms)", [[:walk, 40, 2.0]], { width_ms: 12.0 }, {}, 1, 0],
  ["walk 2.0, 150 ms stalls every 20 reads", [[:walk, 40, 2.0]], {}, { stall_every: 20, stall_ms: 150 }, 1, 0],
  ["walk, pause 5 s, walk", [[:walk, 10, 2.0], [:still, 5], [:walk, 10, 1.8]], {}, {}, 2, 0],
  ["still 120 s (noise 0.004 g)", [[:still, 120]], { noise: 0.004 }, {}, 0, 0],
  ["still 120 s (noise 0.03 g)", [[:still, 120]], { noise: 0.03 }, {}, 0, 0],
  ["still 60 s, slowly tilted 90 deg", [[:still, 60]], { noise: 0.004, tilt: Math::PI / 2 }, {}, 0, 0],
]

failed = 0
all_delays = []
puts format("%-40s %5s %5s %5s %5s  %s", "case", "true", "found", "miss", "extra", "delay ms (min / median / p90 / max)")
CASES.each_with_index do |(name, segs, gen, run, miss_ok, extra_ok), idx|
  w = Walk.new(1000 + idx)
  samples, steps = w.generate(segs, **gen)
  det = StepDetector.new
  found = w.run(det, samples, **run)
  delays, extra, missed = match(found, steps)
  all_delays.concat(delays) if name.start_with?("walk")
  ok = missed <= miss_ok && extra <= extra_ok && det.count == found.size
  failed += 1 unless ok
  d = delays.empty? ? "-" : format("%d / %d / %d / %d", delays.min, pct(delays, 0.5), pct(delays, 0.9), delays.max)
  puts format("%-40s %5d %5d %5d %5d  %s%s", name, steps.size, found.size, missed, extra, d, ok ? "" : "  FAIL")
  if VERBOSE && !found.empty?
    puts "    cadence at the end: #{det.cadence(found.last)} steps/min"
  end
end
puts format("all walking: %d steps, delay median %d ms, p90 %d ms, max %d ms",
            all_delays.size, pct(all_delays, 0.5), pct(all_delays, 0.9), all_delays.max || 0)

# Cadence: 2 steps/s regular is 120 steps/min.
det = StepDetector.new
w = Walk.new(7)
samples, = w.generate([[:walk, 10, 2.0]], jitter: 0.0, amp_var: 0.0)
found = w.run(det, samples)
cad = det.cadence(found.last)
unless (115..125).cover?(cad)
  puts "cadence at 2.0 steps/s: #{cad} (expected about 120)  FAIL"
  failed += 1
else
  puts "cadence at 2.0 steps/s: #{cad} steps/min"
end
puts "cadence 3 s after the last step: #{det.cadence(found.last + 3000)}"
failed += 1 unless det.cadence(found.last + 3000) == 0

# reset_count keeps detecting.
det.reset_count
failed += 1 unless det.count == 0 && det.cadence(found.last) == 0

if failed > 0
  puts "#{failed} case(s) failed"
  exit 1
end
puts "all cases passed"
