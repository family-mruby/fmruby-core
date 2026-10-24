# Checks the step detector of flash/app/test/step_sensor.app.rb against
# synthetic accelerometer data: walking at 1.5 to 2.5 steps per second,
# lying still, noise, slow tilting, a walk with a pause, and shaking the board
# hard for a while and then putting it down (dyn must come back to 0 and a
# walk right after must count as usual), and the accel values for a plot
# (every put whose steps went up has dyn_peak >= rise). Prints the count and the delay of
# each case and exits non-zero when a case is off.
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
  ["still 300 s (noise 0.01 g)", [[:still, 300]], {}, {}, 0, 0],
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

# The raw values for a plot (accel, 20 Hz): as in the app, every RAW_EVERY
# samples the last one is kept (dyn) and, after the read, put with
# dyn_peak = take_peak (the largest dyn since the previous put). Every put
# whose steps went up must show dyn_peak >= rise; dyn alone (the value at
# the put) usually does not, which is why the panel plots dyn_peak.
RAW_EVERY = 5
def raw_puts(w, det, samples, poll_ms: 10, poll_jitter: 6, stall_every: 40, stall_ms: 60)
  rng = Random.new(77)
  puts_ = []
  t = 0.0
  next_index = 0
  reads = 0
  raw_n = 0
  ready = false
  snap = 0.0
  while t <= samples.last[0]
    last = [(t / Walk::ODR_MS).floor, samples.size - 1].min
    if last >= next_index
      now = t.round + 2
      n = last - next_index + 1
      (next_index..last).each_with_index do |i, k|
        s = samples[i]
        det.feed(now - (n - 1 - k) * Walk::ODR_MS, s[1], s[2], s[3])
        raw_n += 1
        next if raw_n < RAW_EVERY
        raw_n = 0
        snap = det.dynamic
        ready = true
      end
      next_index = last + 1
      if ready
        ready = false
        pk = det.take_peak || snap
        puts_ << [det.count, (pk * 1000).round, (snap * 1000).round]
      end
    end
    reads += 1
    t += poll_ms + rng.rand * poll_jitter
    t += stall_ms if stall_every > 0 && reads % stall_every == 0
  end
  puts_
end

puts format("%-40s %7s %9s %12s", "accel puts (dyn_peak)", "steps", "peak<rise", "dyn<rise")
CASES.each_with_index do |(name, segs, gen, run), idx|
  next unless name.start_with?("walk")
  w = Walk.new(1000 + idx)
  samples, = w.generate(segs, **gen)
  det = StepDetector.new
  rise = (det.rise * 1000).round
  prev = 0
  stepped = 0
  bad_peak = 0
  bad_dyn = 0
  raw_puts(w, det, samples, **run.slice(:poll_ms, :poll_jitter, :stall_every, :stall_ms)).each do |steps, pk, dyn|
    if steps > prev
      stepped += 1
      bad_peak += 1 if pk < rise
      bad_dyn += 1 if dyn < rise
    end
    prev = steps
  end
  ok = bad_peak == 0 && stepped > 0
  failed += 1 unless ok
  puts format("%-40s %7d %9d %12d%s", name, stepped, bad_peak, bad_dyn, ok ? "" : "  FAIL")
end

# Shaking: the board shaken by hand for some seconds (a sine along a random
# direction, its amplitude wandering, with a second harmonic, the axes
# clipped at the sensor's range), then put down. The shaking itself counts
# as steps (nothing tells them apart); what matters is what follows: dyn
# (averaged over 500 ms) back within SHAKE_SETTLE_G of 0 within
# SHAKE_SETTLE_MS of the rest, no step
# at rest, and a walk after a short rest counted as usual.
SHAKE_SETTLE_G = 0.02
SHAKE_SETTLE_MS = 5000
SHAKE_AVG = 50 # 500 ms
RANGE_G = 8.0

# along: :g shakes along gravity (up and down), :any in a random direction.
def shake_samples(rng, w, secs, freq, amp, along, rest_ms, noise: 0.01, t0: 0)
  g = w.unit_vector
  u = along == :g ? g : w.unit_vector
  out = []
  t = t0
  t_end = t0 + (secs * 1000).round
  phase = 0.0
  a = amp
  while t < t_end
    phase += 2 * Math::PI * freq * (1.0 + 0.1 * (rng.rand * 2 - 1)) * 0.01
    a += (amp * (1.0 + 0.3 * (rng.rand * 2 - 1)) - a) * 0.05
    k = a * (Math.sin(phase) + 0.3 * Math.sin(2 * phase + 0.7))
    out << [t, *[0, 1, 2].map { |i| (g[i] + u[i] * k + noise * w.gauss).clamp(-RANGE_G, RANGE_G) }]
    t += Walk::ODR_MS
  end
  rest_from = t
  while t < rest_from + rest_ms
    out << [t, *[0, 1, 2].map { |i| g[i] + noise * w.gauss }]
    t += Walk::ODR_MS
  end
  [out, rest_from, g]
end

# Feeds samples as read by the app (every 10 ms here); returns
# [step times, [[t, dyn]]].
def feed_all(det, samples)
  steps = []
  dyns = []
  samples.each do |t, x, y, z|
    steps << t if det.feed(t, x, y, z)
    dyns << [t, det.dynamic]
  end
  [steps, dyns]
end

SHAKES = [
  # seconds, frequency (Hz), amplitude (g)
  [5, 2.0, 0.5, :g], [5, 3.0, 1.0, :g], [8, 4.0, 1.5, :g], [5, 5.0, 2.0, :g], [10, 6.0, 3.0, :g],
  [5, 3.0, 6.0, :g], [5, 2.0, 0.5, :any], [5, 3.0, 1.0, :any], [8, 4.0, 1.5, :any],
  [5, 5.0, 2.0, :any], [5, 8.0, 4.0, :any], [5, 3.0, 6.0, :any], [20, 2.5, 0.8, :any],
  [3, 10.0, 1.0, :any],
]
puts format("%-32s %7s %10s %12s %10s", "shake then rest", "steps", "recover ms", "rest steps", "dyn at 5 s")
recovers = []
SHAKES.each_with_index do |(secs, freq, amp, along), idx|
  [0.004, 0.03].each do |noise|
    rng = Random.new(5000 + idx)
    w = Walk.new(5000 + idx)
    samples, rest_from, = shake_samples(rng, w, secs, freq, amp, along, 30000, noise: noise)
    det = StepDetector.new
    steps, dyns = feed_all(det, samples)
    # The last sample (at rest) with dyn off by more than SHAKE_SETTLE_G,
    # dyn averaged over SHAKE_AVG samples (the noise alone moves it by about 20 mg
    # at 0.03 g).
    avg = []
    dyns.each_with_index do |(t, _), i|
      next if t < rest_from
      win = dyns[[i - SHAKE_AVG + 1, 0].max..i]
      avg << [t, win.sum { |_, d| d } / win.size]
    end
    last_off = avg.select { |_, d| d.abs > SHAKE_SETTLE_G }.map(&:first).max
    rec = last_off ? last_off + Walk::ODR_MS - rest_from : 0
    # Rest starts at the first sample of rest; the drop to 1 g is the
    # shaking's last movement and may count as its last step.
    rest_steps = steps.count { |t| t > rest_from + 100 }
    d5 = avg.find { |t, _| t >= rest_from + SHAKE_SETTLE_MS }[1]
    ok = rec <= SHAKE_SETTLE_MS && rest_steps == 0
    failed += 1 unless ok
    recovers << rec
    puts format("%-32s %7d %10d %12d %8d mg%s", format("%ds %.1f Hz %.1f g %s n%.3f", secs, freq, amp, along, noise),
                steps.count { |t| t <= rest_from + 100 }, rec, rest_steps, (d5 * 1000).round, ok ? "" : "  FAIL")
  end
end
puts format("recover (dyn within %d mg for good): median %d ms, max %d ms",
            (SHAKE_SETTLE_G * 1000).round, pct(recovers, 0.5), recovers.max)

# A walk right after shaking: 3 s at rest, then 20 s at 2 steps/s.
[[5, 3.0, 1.0, :g], [8, 4.0, 1.5, :g], [5, 3.0, 6.0, :any], [20, 2.5, 0.8, :any]].each_with_index do |(secs, freq, amp, along), idx|
  rng = Random.new(6000 + idx)
  w = Walk.new(6000 + idx)
  shake, rest_from, = shake_samples(rng, w, secs, freq, amp, along, 2000)
  walk, true_steps = w.generate([[:walk, 20, 2.0]])
  # Walk starts after its own second of rest: 3 s at rest in all.
  off = shake.last[0] + Walk::ODR_MS
  walk.each { |s| s[0] += off }
  true_steps = true_steps.map { |t| t + off }
  det = StepDetector.new
  found, = feed_all(det, shake + walk)
  found = found.select { |t| t > rest_from + 100 }
  delays, extra, missed = match(found, true_steps)
  ok = missed <= 1 && extra == 0
  failed += 1 unless ok
  puts format("walk after shaking %ds %.1f Hz %.1f g %s: %d steps, found %d, miss %d, extra %d, delay median %d ms%s",
              secs, freq, amp, along, true_steps.size, found.size, missed, extra, pct(delays, 0.5), ok ? "" : "  FAIL")
end

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
