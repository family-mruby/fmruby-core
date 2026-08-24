# Service list reading and bookkeeping for the service host (services.app.rb).
#
# Everything in this file is plain Ruby over String, Hash, Array and Integer:
# no files, no messages, no clock, no FmrbApp. That is deliberate. It is the
# part that decides behaviour -- how the two layers merge, when a oneshot
# leaves the list, how many errors a service gets before it is switched off --
# and keeping it free of the machine lets the same source run under host Ruby
# in test/services/run.rb, without docker or a device.
#
# The host owns the array of SvcEntry and does the talking; this file owns the
# rules.

module SvcConf
  # How many times a service may raise before it is switched off for good.
  # One is not enough: a service that touches hardware can lose a race during
  # boot and then work for the rest of the session, and dropping it for that
  # would be worse than the exception it logged.
  ERROR_LIMIT = 3

  # A handler slower than this holds up every other service (they run one
  # after another on one task), so it is called out in the log.
  SLOW_MS = 50

  # Floor for the host's own sleep, so a service with a silly interval cannot
  # spin the task. There is no ceiling: the sleep runs to the nearest deadline
  # however far off it is.
  MIN_SLEEP_MS = 50

  # What the host sleeps when no service has a deadline at all (a list of
  # nothing but subscribers). It is not a poll interval -- _spin delivers
  # messages throughout the sleep, so on_control still runs at once and
  # nothing waits on this. Waking is only for ticks, so with no tick due
  # there is nothing to wake for.
  IDLE_SLEEP_MS = 30000

  # Parse the subset of toml a service list uses: "[name]" tables,
  # "[name.config]" sub-tables, and "key = value" lines with string, integer
  # and boolean values.
  #
  # Written here rather than bound to the C parser (fmrb_toml, used by the
  # spawner) because a service list is two small files read once at boot, and
  # a Ruby reader keeps the host self-contained -- there is no Ruby binding
  # for fmrb_toml and adding one to read ten lines is not worth a C API.
  #
  # A line that does not parse is skipped, not raised over: a typo in one
  # entry of a hand-written list must not stop the services spelled correctly.
  #
  # Returns { "name" => { "file" => "chime.rb", ..., "config" => {...} } } in
  # file order.
  def self.parse(text)
    out = {}
    return out unless text
    sec = nil    # table the following key/value lines belong to
    cfg = nil    # non-nil while inside [name.config]
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = lines[i].strip
      i += 1
      next if line.empty?
      next if line.start_with?("#")

      if line.start_with?("[")
        close = line.index("]")
        next unless close
        next if close < 2
        path = line[1, close - 1].strip
        next if path.empty?
        dot = path.index(".")
        if dot
          # Only [name.config] is understood; any other sub-table is skipped
          # along with the lines under it (sec = nil).
          base = path[0, dot]
          rest = path[dot + 1, path.length - dot - 1]
          if rest == "config" && !base.empty?
            entry = out[base]
            unless entry
              entry = {}
              out[base] = entry
            end
            sub = entry["config"]
            unless sub
              sub = {}
              entry["config"] = sub
            end
            sec = entry
            cfg = sub
          else
            sec = nil
            cfg = nil
          end
        else
          entry = out[path]
          unless entry
            entry = {}
            out[path] = entry
          end
          sec = entry
          cfg = nil
        end
        next
      end

      next unless sec
      eq = line.index("=")
      next unless eq
      next if eq == 0
      key = line[0, eq].strip
      next if key.empty?
      val = parse_value(line[eq + 1, line.length - eq - 1].strip)
      if cfg
        cfg[key] = val
      else
        sec[key] = val
      end
    end
    out
  end

  # One toml scalar: "quoted string", true / false, an integer, or -- for
  # anything else -- the bare text, so an unquoted word still reaches the
  # service instead of turning into 0.
  def self.parse_value(raw)
    return "" if raw.nil?
    s = raw
    if s.start_with?("\"")
      body = s[1, s.length - 1]
      close = body.index("\"")
      return close ? body[0, close] : body
    end
    hash = s.index("#")
    s = s[0, hash].strip if hash
    return true if s == "true"
    return false if s == "false"
    return s.to_i if integer?(s)
    s
  end

  # to_i answers 0 for "abc" as happily as for "0", so the digits are checked
  # before the conversion is trusted.
  def self.integer?(s)
    return false if s.nil? || s.empty?
    i = 0
    i = 1 if s.start_with?("-")
    return false if i >= s.length
    while i < s.length
      b = s.getbyte(i)
      return false if b < 48 || b > 57
      i += 1
    end
    true
  end

  # System list first, user list on top. A user may change any field of a
  # system service -- "enable = false" is what switches one off -- and the
  # origin stays with the layer that introduced it, so ps can say where a
  # service came from even when the user has edited its settings.
  def self.merge(sys, usr)
    out = {}
    keys = sys.keys
    i = 0
    while i < keys.size
      k = keys[i]
      out[k] = with_origin(sys[k], "sys")
      i += 1
    end
    keys = usr.keys
    i = 0
    while i < keys.size
      k = keys[i]
      base = out[k]
      if base
        out[k] = overlay(base, usr[k])
      else
        out[k] = with_origin(usr[k], "usr")
      end
      i += 1
    end
    out
  end

  def self.with_origin(conf, origin)
    out = {}
    keys = conf.keys
    i = 0
    while i < keys.size
      k = keys[i]
      out[k] = k == "config" ? copy_config(conf[k]) : conf[k]
      i += 1
    end
    out["origin"] = origin
    out
  end

  def self.copy_config(cfg)
    out = {}
    return out unless cfg
    keys = cfg.keys
    i = 0
    while i < keys.size
      k = keys[i]
      out[k] = cfg[k]
      i += 1
    end
    out
  end

  # The user's fields win one by one, config included: overriding "hour_only"
  # must not silently drop the other settings the system list gave the
  # service.
  def self.overlay(base, over)
    out = {}
    keys = base.keys
    i = 0
    while i < keys.size
      k = keys[i]
      out[k] = base[k]
      i += 1
    end
    keys = over.keys
    i = 0
    while i < keys.size
      k = keys[i]
      if k == "config"
        merged = copy_config(base["config"])
        sub = over[k]
        subkeys = sub ? sub.keys : []
        j = 0
        while j < subkeys.size
          sk = subkeys[j]
          merged[sk] = sub[sk]
          j += 1
        end
        out["config"] = merged
      else
        out[k] = over[k]
      end
      i += 1
    end
    out
  end

  # How long the host may sleep: until the nearest deadline of either kind --
  # a periodic tick or a wake_in that a service asked for -- floored so a bad
  # interval cannot spin the task. No deadline at all means there is nothing
  # to wake for, so it sleeps IDLE_SLEEP_MS.
  def self.next_sleep(entries, now)
    best = nil
    i = 0
    while i < entries.size
      e = entries[i]
      i += 1
      next unless e.running?
      at = e.next_at
      best = at if at && (best.nil? || at < best)
      at = e.wake_at
      best = at if at && (best.nil? || at < best)
    end
    return IDLE_SLEEP_MS if best.nil?
    ms = best - now
    ms < MIN_SLEEP_MS ? MIN_SLEEP_MS : ms
  end
end

# One entry of the merged list: what the toml said, plus the counters ps
# reports and the error budget that switches a misbehaving service off.
#
# The instance the service class was made into lives in @obj. Nothing else in
# this class touches it -- calling into a service is the host's job, because
# that is where the rescue and the timing belong.
class SvcEntry
  RUNNING = "running"
  STOPPED = "stopped"
  FAILED  = "failed"

  attr_reader :name, :origin, :file, :class_name, :interval_ms, :config,
              :app_path, :fullscreen, :delay_ms, :oneshot, :state,
              :ticks, :events, :errors, :wakes, :wake_at
  attr_accessor :obj, :ctx, :next_at, :topics, :slow_count,
                :has_tick, :has_event, :has_stop, :has_wake, :warned_wake

  def initialize(name, conf)
    @name = name
    @origin = (conf["origin"] || "usr").to_s
    @file = conf["file"]
    @class_name = conf["class"]
    @app_path = conf["app"]
    @fullscreen = conf["fullscreen"] ? true : false
    @delay_ms = (conf["delay_ms"] || 0).to_i
    @interval_ms = (conf["interval_ms"] || 0).to_i
    @oneshot = conf["oneshot"] ? true : false
    # enable defaults to true: a list is written to run what it names.
    @enabled = conf.key?("enable") ? (conf["enable"] ? true : false) : true
    @config = conf["config"] || {}
    @obj = nil
    @ctx = nil
    @has_tick = false
    @has_event = false
    @has_stop = false
    @has_wake = false
    @warned_wake = false
    @topics = []
    @next_at = nil
    @wake_at = nil
    @ticks = 0
    @events = 0
    @errors = 0
    @wakes = 0
    @slow_count = 0
    @state = @enabled ? RUNNING : STOPPED
  end

  def enabled?
    @enabled
  end

  # An "app = ..." entry starts an ordinary app at boot instead of running a
  # service inside the host; it has no class and no callbacks.
  def app?
    @app_path ? true : false
  end

  def running?
    @state == RUNNING
  end

  def failed?
    @state == FAILED
  end

  # Nothing to deliver to: never loaded, or switched off.
  def deliverable?
    return false unless @obj
    @state == RUNNING
  end

  def note_tick
    @ticks += 1
    nil
  end

  def note_event
    @events += 1
    nil
  end

  def note_wake
    @wakes += 1
    nil
  end

  # Returns true when this error used the last of the budget, so the caller
  # knows to say so once rather than on every raise afterwards.
  def note_error
    @errors += 1
    return false if @errors < SvcConf::ERROR_LIMIT
    return false if @state == FAILED
    @state = FAILED
    @next_at = nil
    @wake_at = nil
    true
  end

  # Asked for by svc/ctl "stop" and by the service itself (ctx.stop_self).
  def stop
    return false unless @state == RUNNING
    @state = STOPPED
    @next_at = nil
    @wake_at = nil
    true
  end

  # svc/ctl "start". The error count goes back to zero, which is what makes
  # this the way to give a failed service another go.
  def start(now)
    return false if @state == RUNNING
    @state = RUNNING
    @errors = 0
    arm(now)
    true
  end

  def arm(now)
    @next_at = @interval_ms > 0 ? now + @interval_ms : nil
    nil
  end

  # An app entry has no interval: delay_ms is a single wait after boot, for
  # when the desktop should be allowed to settle before the screen is taken.
  def arm_app(now)
    @next_at = now + @delay_ms
    nil
  end

  # ctx.wake_in: one deadline of its own, kept apart from the periodic one.
  #
  # There is room for exactly one pending wake per service, and asking again
  # replaces it. A queue would need a service to reason about how many of its
  # own requests are still in flight, which is the kind of hidden state this
  # contract is trying to keep out; "the latest ask wins" is what a state
  # machine over ticks actually wants.
  #
  # It never touches @next_at: a wake must not shift the periodic schedule,
  # or a service that asks for one would quietly change its own tick rate.
  def wake_in(now, ms)
    delay = ms.to_i
    delay = 0 if delay < 0
    @wake_at = now + delay
    nil
  end

  def wake_due?(now)
    return false unless @wake_at
    return false unless @state == RUNNING
    @wake_at <= now
  end

  # Clears the deadline and says whether it was this call that took it, so a
  # wake fires exactly once even if the service asks for another one from
  # inside on_wake.
  def take_wake
    return false unless @wake_at
    @wake_at = nil
    true
  end

  def due?(now)
    return false unless @next_at
    return false unless @state == RUNNING
    @next_at <= now
  end

  # Report line for svc/ctl "list" (ps prints it under the host's row).
  def to_report
    {
      "name" => @name,
      "origin" => @origin,
      "state" => @state,
      "ticks" => @ticks,
      "events" => @events,
      "wakes" => @wakes,
      "errors" => @errors
    }
  end
end
