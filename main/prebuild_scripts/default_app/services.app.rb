# Service host: one VM for everything that has to stay resident.
#
# A resident job written as its own app costs a FreeRTOS task (12-16 KB of
# internal RAM for its C stack), a whole mrb_state and one of the nine VM
# slots. Three of them are enough to be felt everywhere else, so they share
# this one instead: the host reads the two service lists at boot, holds one
# instance of each service class, and calls into them from its own event loop.
#
# The loop IS the app's own lifecycle -- on_update for the timers, on_control
# for the deliveries -- so there is no scheduler here beyond a deadline per
# service. Being sequential is the point: two services can never race over
# anything, which is the right trade for code a user writes in an afternoon.
# The cost is that a slow handler holds up the others, so every call is timed
# and a slow one is named in the log.
#
# Layout (doc/user_extension/services/plan.md):
#   /etc/services.toml       system list   (generated from config/services.toml)
#   /usr/share/services/     system service sources
#   /home/services.toml      user list     (the user edits this)
#   /home/services/          user service sources
#
# The service contract is four optional methods -- on_start(ctx), on_tick(now_ms),
# on_event(topic, data), on_stop -- and ctx is the only way back out.

# What a service is given. Everything a service may do to the rest of the
# machine goes through one of these five calls, which is what keeps a service
# from having to know it lives inside a host at all.
class SvcCtx
  def initialize(host, entry)
    @host = host
    @entry = entry
    @name = entry.name
    @config = entry.config
  end

  # To Pub/Sub, and to the services alongside this one.
  #
  # Both, because the kernel does not deliver a message back to the pid that
  # sent it -- and every service in this host shares that one pid. Without the
  # local half, a service could be heard by every app on the machine except
  # the ones sitting next to it, which is exactly the pairing the samples are
  # built on (clock -> hourly_chime, net -> timesync).
  def publish(topic, data = nil)
    @host.svc_publish(topic, data)
    nil
  end

  def log(msg)
    Log.info("svc[#{@name}] #{msg}")
    nil
  end

  def now_ms
    Machine.board_millis
  end

  # The [<name>.config] table of the service list, as a Hash with String keys.
  def config
    @config
  end

  # Ask to be called back once, ms from now, in on_wake.
  #
  # This is how anything with a shape in time is written: a handler must
  # return at once, so "start a note, end it 200 ms later" is a wake, not a
  # sleep and not a fast periodic tick that does nothing 99% of the time.
  # One pending wake per service; asking again replaces it. It does not
  # disturb interval_ms.
  def wake_in(ms)
    @host.svc_wake_in(@entry, ms)
    nil
  end

  # The host's own FmrbAudio, shared by every service that makes a sound.
  # Without this a service could not reach the APU at all: FmrbAudio is built
  # on an app, and a service deliberately never sees one.
  def audio
    @host.svc_audio
  end

  # Take myself out of the delivery list. For a service that finds it has
  # nothing to do on this machine (no hardware, no file) and would otherwise
  # log the same complaint every tick.
  def stop_self
    @host.stop_service(@name, "asked to stop itself")
    nil
  end
end

class ServicesApp < FmrbApp
  SYS_TOML = "/etc/services.toml"
  SYS_DIR  = "/usr/share/services"
  USR_TOML = "/home/services.toml"
  USR_DIR  = "/home/services"
  # The host's own file, and the only one it writes: which services the user
  # has switched on and off, so the choice outlives a reboot. Their list stays
  # exactly as they typed it (see SvcConf.parse_state).
  USR_STATE = "/home/services_state.toml"

  # Where ps / kill / svc talk to the host. Requests carry a "reply_to" topic
  # the caller subscribes to, so several tools can ask at once without the
  # host having to know any of them.
  CTL_TOPIC = "svc/ctl"

  # System topic the kernel publishes when any app ends
  # (doc/user_extension/services/plan.md). Subscribed only when a list has an
  # "app =" entry asking to be restarted -- there is nothing to do with it
  # otherwise, and a subscription costs the kernel a delivery per death.
  DIED_TOPIC = "app/died"

  # Same guard the kernel keeps for the host itself, and for the same reason:
  # an app that dies the moment it starts must not be able to spin the machine.
  RESTART_DELAY_MS = 2000
  RESTART_WINDOW_MS = 300000
  RESTART_LIMIT = 3

  def on_create
    @audio = nil        # built on demand; most lists make no sound
    @state = {}         # name => on/off, from and to the state file
    # An enable that has to load a file is finished from on_update, not from
    # the control message that asked for it (see handle_enable).
    @pending_enable = nil
    @pending_enable_reply = nil
    # Topics published by one service and destined for another in this host,
    # as a flat [topic, data, topic, data, ...] queue.
    @local = []
    @services = []      # SvcEntry, in list order
    @apps = []          # SvcEntry with "app =", waiting for their delay
    @topics = []        # what this host is subscribed to on behalf of services
    now = Machine.board_millis

    conf = read_lists
    load_all(conf, now)

    subscribe(CTL_TOPIC)
    subscribe(DIED_TOPIC) if any_restart?
    Log.info("services: #{running_count} running, #{@services.size - running_count} " \
             "disabled, #{@apps.size} app(s) to start")
    nil
  end

  # SvcCtx#wake_in. A service with no on_wake asking for one is almost
  # certainly a bug -- the callback it means to reach is spelled wrong, or it
  # thinks the wake arrives as a tick -- so it is said once and the request is
  # dropped rather than kept for a method that will never be called.
  def svc_wake_in(entry, ms)
    unless entry.has_wake
      unless entry.warned_wake
        entry.warned_wake = true
        Log.warn("svc[#{entry.name}] called wake_in but defines no on_wake; ignored")
      end
      return nil
    end
    entry.wake_in(Machine.board_millis, ms)
    # Most wakes are asked for from on_event, which runs inside _spin -- and
    # _spin does not return early on its own, so without this the callback
    # would not be looked at until the current sleep ran out. With the idle
    # sleep at 30 s that turned a 200 ms chime into a 30 second one.
    request_early_update
    nil
  end

  # SvcCtx#publish. Out to the rest of the machine, and queued for the
  # services in this host.
  #
  # Queued rather than delivered on the spot: publish is normally called from
  # inside a handler, and calling on_event from there would run one service
  # inside another -- nesting that a service publishing from its own on_event
  # could carry to any depth. The queue keeps the promise that services run
  # one after another, and drain_local is what enforces the bound.
  def svc_publish(topic, data)
    publish(topic, data)
    t = topic.to_s
    return nil unless @topics.index(t)
    @local << t
    @local << data
    request_early_update
    nil
  end

  # How many local deliveries one turn of the loop will do. Services that
  # answer each other's topics are the point of the mechanism, so a few rounds
  # are normal; a service that republishes what it hears is a loop, and this
  # is what stops it from taking the host with it.
  LOCAL_MAX_PER_TURN = 32

  def drain_local(now)
    n = 0
    while @local.size > 0 && n < LOCAL_MAX_PER_TURN
      topic = @local.shift
      data = @local.shift
      deliver(topic, data)
      n += 1
    end
    return nil if @local.size == 0
    Log.warn("services: #{@local.size} queued deliveries left after #{LOCAL_MAX_PER_TURN}; a service may be answering itself")
    @local = []
    nil
  end

  # One FmrbAudio for the whole host (SvcCtx#audio). Built on first ask: it
  # costs nothing to make, but a machine whose services are all silent should
  # not carry one at all.
  def svc_audio
    @audio = FmrbAudio.new(self) unless @audio
    @audio
  end

  # ---- boot ----------------------------------------------------------------

  # Three layers, in this order: the system list, the user's list on top of it,
  # and the state file on top of both.
  def read_lists
    sys = SvcConf.parse(read_file(SYS_TOML))
    usr = SvcConf.parse(read_file(USR_TOML))
    conf = SvcConf.merge(sys, usr)
    @state = SvcConf.parse_state(read_file(USR_STATE))
    unknown = SvcConf.apply_state(conf, @state)
    # A name left in the state file after its service was deleted is not an
    # error -- said once and ignored, so an old file cannot stop the machine.
    Log.info("services: state file mentions unknown service(s): #{unknown.join(', ')}") if unknown.size > 0
    conf
  end

  # Remember a switch and write it down. The file is replaced through a
  # temporary and a rename, the same way the desktop saves its config: a power
  # cut mid-write leaves the old file, not half of a new one.
  def record_state(name, on)
    @state[name] = on
    tmp = "#{USR_STATE}.tmp"
    f = nil
    begin
      f = File.open(tmp, "w")
      f.write(SvcConf.render_state(@state))
      f.close
      f = nil
      File.rename(tmp, USR_STATE)
      true
    rescue => e
      Log.error("services: cannot write #{USR_STATE}: #{e.message}")
      begin
        f.close if f
        File.unlink(tmp)
      rescue
      end
      false
    end
  end

  def read_file(path)
    f = nil
    begin
      f = File.open(path, "r")
      text = f.read
      f.close
      f = nil
      text
    rescue => e
      # Missing is the normal case for both files -- a machine with no user
      # services has no /home/services.toml -- so this is not an error.
      Log.info("services: no #{path} (#{e.message})")
      f.close if f
      nil
    end
  end

  def load_all(conf, now)
    names = conf.keys
    i = 0
    while i < names.size
      name = names[i]
      i += 1
      entry = SvcEntry.new(name, conf[name])
      if entry.app?
        # An app entry that is switched off simply does not start. There is no
        # instance to keep, so it is not in the list either: switching it back
        # on is for the next boot (or start it by hand).
        unless entry.enabled?
          Log.info("services: #{name} disabled")
          next
        end
        entry.arm_app(now)
        @apps << entry
        next
      end
      unless entry.enabled?
        # Kept in the list, not loaded: ps shows it as disabled, and
        # "svc enable" has something to find and load.
        Log.info("services: #{name} disabled")
        @services << entry
        next
      end
      next unless load_service(entry, now)
      if entry.oneshot
        # Ran once and that was the whole job: no tick, no delivery, no line
        # in the service list.
        Log.info("services: #{name} oneshot done")
        next
      end
      @services << entry
    end
    nil
  end

  # Bring one service in: require its file, make the instance, remember which
  # of the four methods it actually has, subscribe what it asked for, call
  # on_start. Anything that goes wrong here takes out this service only.
  def load_service(entry, now)
    path = service_path(entry)
    unless path
      Log.warn("services: #{entry.name} has neither file= nor app=")
      return false
    end
    begin
      require(path)
    rescue StandardError, ScriptError => e
      # ScriptError as well: a service file with a typo in it raises
      # SyntaxError from the compile inside require, and a missing require of
      # its own raises LoadError. Both are ScriptError, not StandardError, so
      # a plain `rescue => e` lets them past and the host dies for one bad
      # file (see the note above call_service).
      Log.error("services: cannot load #{entry.name} (#{path}): #{e.class}: #{e.message}")
      return false
    end

    cname = entry.class_name
    unless cname
      Log.error("services: #{entry.name} has no class= in the service list")
      return false
    end
    begin
      klass = Object.const_get(cname)
      entry.obj = klass.new
    rescue StandardError, ScriptError => e
      Log.error("services: cannot start #{entry.name} (#{cname}): #{e.class}: #{e.message}")
      return false
    end

    obj = entry.obj
    entry.has_tick = obj.respond_to?(:on_tick)
    entry.has_event = obj.respond_to?(:on_event)
    entry.has_stop = obj.respond_to?(:on_stop)
    entry.has_wake = obj.respond_to?(:on_wake)
    entry.ctx = SvcCtx.new(self, entry)
    entry.topics = declared_topics(klass)
    subscribe_topics(entry)
    entry.arm(now)

    # An on_start that raises costs the service one of its three errors, not
    # its place in the list: a job that loses a race against some piece of
    # hardware during boot works for the rest of the session.
    call_service(entry, 0, entry.ctx, nil) if obj.respond_to?(:on_start)
    Log.info("services: #{entry.name} started (#{entry.origin}, #{path})")
    true
  end

  # "file" is relative to the directory of the layer the service came from, so
  # a user service and a system service of the same name never collide.
  def service_path(entry)
    file = entry.file
    return nil unless file
    name = file.to_s
    # require adds the extension itself and would look for "x.rb.rb".
    name = name[0, name.length - 3] if name.end_with?(".rb")
    return name if name.start_with?("/")
    dir = entry.origin == "sys" ? SYS_DIR : USR_DIR
    "#{dir}/#{name}"
  end

  # SUBSCRIBE is a constant on the service class rather than a method, so the
  # host knows what to subscribe to before it has called anything.
  def declared_topics(klass)
    out = []
    begin
      return out unless klass.const_defined?(:SUBSCRIBE)
      list = klass.const_get(:SUBSCRIBE)
      return out unless list.is_a?(Array)
      i = 0
      while i < list.size
        t = list[i].to_s
        out << t unless t.empty?
        i += 1
      end
    rescue StandardError, ScriptError => e
      Log.warn("services: bad SUBSCRIBE list: #{e.class}: #{e.message}")
    end
    out
  end

  # One subscription per topic no matter how many services want it: the
  # kernel delivers to a pid, and this host is one pid.
  def subscribe_topics(entry)
    list = entry.topics
    i = 0
    while i < list.size
      t = list[i]
      i += 1
      next if @topics.index(t)
      @topics << t
      subscribe(t)
    end
    nil
  end

  def running_count
    n = 0
    i = 0
    while i < @services.size
      n += 1 if @services[i].running?
      i += 1
    end
    n
  end

  def any_restart?
    i = 0
    while i < @apps.size
      return true if @apps[i].restart
      i += 1
    end
    false
  end

  # ---- calling into a service ---------------------------------------------

  # The single door into service code. It times the call, counts it, and keeps
  # an exception inside the service that raised it: everything else keeps
  # running, and three errors switch that one service off for good.
  #
  # kind: 0 on_start / 1 on_tick / 2 on_event / 3 on_stop / 4 on_wake.
  # Returns false when the call raised.
  #
  # ScriptError is caught alongside StandardError, and the pair is the whole
  # of what a badly written service throws in practice: NotImplementedError
  # (an API this machine does not have -- Machine.set_hwclock in the
  # simulator is one, and it is a ScriptError, so a plain `rescue => e` used
  # to let it past and kill the host for one service's mistake), LoadError and
  # SyntaxError. Catching them keeps S1's promise that one bad service does
  # not take the others with it.
  #
  # Bare Exception is deliberately NOT caught. It is what remains for the
  # things a rescue here cannot honestly handle -- SystemStackError arrives
  # mid-unwind, and a service that raises Exception itself is saying the host
  # should end, which is how the kernel's restart path is exercised
  # (doc/user_extension/services/report/s2.md).
  def call_service(entry, kind, a, b)
    obj = entry.obj
    return false unless obj
    t0 = Machine.board_millis
    ok = true
    begin
      case kind
      when 0
        obj.on_start(a)
      when 1
        obj.on_tick(a)
        entry.note_tick
      when 2
        obj.on_event(a, b)
        entry.note_event
      when 3
        obj.on_stop
      when 4
        obj.on_wake(a)
      end
    rescue StandardError, ScriptError => e
      ok = false
      Log.error("svc[#{entry.name}] #{e.class}: #{e.message}")
      if entry.note_error
        Log.error("svc[#{entry.name}] disabled after #{SvcConf::ERROR_LIMIT} errors")
        drop_service(entry)
      end
    end
    spent = Machine.board_millis - t0
    warn_slow(entry, spent) if spent > SvcConf::SLOW_MS
    ok
  end

  # A service that holds the loop delays every other service and the answers
  # to svc/ctl. Said once, then once every ten times, so a service that is
  # slow by nature does not bury the log.
  def warn_slow(entry, spent)
    entry.slow_count += 1
    n = entry.slow_count
    return nil unless n == 1 || n % 10 == 0
    Log.warn("svc[#{entry.name}] took #{spent} ms (over #{SvcConf::SLOW_MS}); count=#{n}")
    nil
  end

  # Failed for good: the instance is kept (svc start can bring it back) but it
  # stops being delivered to.
  def drop_service(entry)
    entry.next_at = nil
    nil
  end

  # ---- the loop ------------------------------------------------------------

  def on_update
    now = Machine.board_millis
    begin
      # Before the ticks: an enable that is waiting to load should be running
      # by the time anything else this turn looks at the list.
      finish_enable(now)
      # Before the ticks, so a service that published during the last turn is
      # heard before anything new happens.
      drain_local(now)
      start_due_apps(now)
      run_due_ticks(now)
      run_due_wakes(now)
      # And again after, so a publish made during this turn's ticks does not
      # wait for the next one.
      drain_local(now)
    rescue => e
      # Not the services -- call_service already keeps their exceptions --
      # but the host's own work around them. It must outlive that too: an
      # exception reaching main_loop ends the app, and with it every service.
      Log.error("services: update failed: #{e.class}: #{e.message}")
    end
    a = SvcConf.next_sleep(@services, now)
    b = SvcConf.next_sleep(@apps, now)
    a < b ? a : b
  end

  def run_due_ticks(now)
    i = 0
    while i < @services.size
      entry = @services[i]
      i += 1
      next unless entry.due?(now)
      # Re-armed from the deadline that just passed, not from "now": a tick
      # that ran long must not push the next one out by the overrun.
      entry.arm(now)
      next unless entry.has_tick
      call_service(entry, 1, now, nil)
    end
    nil
  end

  # Wakes are walked after the ticks and taken before the call, so a service
  # that asks for another wake from inside on_wake keeps it: the deadline it
  # just set is not the one being cleared.
  def run_due_wakes(now)
    i = 0
    while i < @services.size
      entry = @services[i]
      i += 1
      next unless entry.wake_due?(now)
      next unless entry.take_wake
      entry.note_wake
      call_service(entry, 4, now, nil)
    end
    nil
  end

  # ---- app entries ---------------------------------------------------------

  # "app = /path" starts an ordinary app at boot. The host only asks; from
  # then on it is a normal app, visible in ps and endable with kill <pid>, and
  # the host does not watch it.
  def start_due_apps(now)
    i = 0
    while i < @apps.size
      entry = @apps[i]
      i += 1
      next unless entry.due?(now)
      entry.stop           # asked for once; drop out of the deadline list
      entry.started_pid = 0
      req = { "cmd" => "spawn", "app_name" => entry.app_path.to_s }
      # Overrides the window mode the app's own .toml asks for, which is what
      # makes "boot straight into this game" work without editing the app.
      req["fullscreen"] = true if entry.fullscreen
      again = entry.restarts > 0 ? " (restart #{entry.restarts})" : ""
      Log.info("services: starting app #{entry.app_path}#{entry.fullscreen ? ' (fullscreen)' : ''}#{again}")
      send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, req)
    end
    nil
  end

  # The kernel's answer to a spawn request: which pid the app got. Kept so
  # app/died can be matched to the entry that asked for it -- the topic
  # carries a pid, and nothing else about a running app is ours to recognise.
  def note_spawn_result(path, pid)
    return nil unless path
    return nil unless pid
    i = 0
    while i < @apps.size
      entry = @apps[i]
      i += 1
      next unless entry.app_path.to_s == path.to_s
      entry.started_pid = pid.to_i
      Log.info("services: #{entry.name} is pid #{pid}")
      return nil
    end
    nil
  end

  # An app ended. Only the entries that started one and asked to have it back
  # care, and only when the death was not asked for: a kill must stay killed.
  def handle_app_died(data)
    return nil unless data.is_a?(Hash)
    pid = data["pid"]
    return nil unless pid
    entry = find_started_app(pid)
    return nil unless entry
    entry.started_pid = 0
    now = Machine.board_millis
    expected = data["expected"] ? true : false
    unless entry.note_app_death(now, expected, RESTART_WINDOW_MS, RESTART_LIMIT)
      if expected
        Log.info("services: #{entry.name} (#{data["name"]}) was ended on request")
      elsif entry.restart
        Log.error("services: #{entry.name} died #{RESTART_LIMIT} times, giving up")
      end
      return nil
    end
    Log.warn("services: #{entry.name} died unexpectedly (#{entry.death_count}); " \
             "restarting in #{RESTART_DELAY_MS} ms")
    entry.arm_restart(now, RESTART_DELAY_MS)
    # The deadline was set from inside on_control, which runs inside _spin.
    request_early_update
    nil
  end

  def find_started_app(pid)
    i = 0
    while i < @apps.size
      entry = @apps[i]
      return entry if entry.started_pid != 0 && entry.started_pid == pid
      i += 1
    end
    nil
  end

  # ---- deliveries and control ---------------------------------------------

  def on_control(msg)
    if msg["cmd"] == "spawn_result"
      note_spawn_result(msg["app"], msg["pid"])
      return nil
    end
    return nil unless msg["cmd"] == "topic_data"
    topic = msg["topic"]
    begin
      if topic == CTL_TOPIC
        handle_ctl(msg["data"])
      elsif topic == DIED_TOPIC
        handle_app_died(msg["data"])
      else
        deliver(topic, msg["data"])
      end
    rescue => e
      # Same reason as on_update: one malformed request must not be able to
      # end the host. It could -- a reply that did not fit in a message
      # raised out of main_loop and took all three services down with it.
      Log.error("services: control failed: #{e.class}: #{e.message}")
    end
    nil
  end

  def deliver(topic, data)
    i = 0
    while i < @services.size
      entry = @services[i]
      i += 1
      next unless entry.deliverable?
      next unless entry.has_event
      next unless entry.topics.index(topic)
      call_service(entry, 2, topic, data)
    end
    nil
  end

  # svc/ctl: list / stop / start. The answer goes to the topic the caller
  # named, so nothing here has to know who asked.
  def handle_ctl(req)
    return nil unless req.is_a?(Hash)
    cmd = req["cmd"]
    reply_to = req["reply_to"]
    name = req["name"]
    case cmd
    when "list"
      send_list(reply_to)
    when "stop"
      entry = find_service(name)
      if entry.nil?
        answer(reply_to, false, name, "no such service")
      elsif entry.disabled?
        answer(reply_to, false, name, "already disabled")
      elsif stop_service(name, "asked by svc/ctl")
        answer(reply_to, true, name, nil)
      else
        answer(reply_to, false, name, "already stopped")
      end
    when "enable"
      handle_enable(reply_to, name)
    when "disable"
      handle_disable(reply_to, name)
    when "start"
      entry = find_service(name)
      if entry.nil?
        answer(reply_to, false, name, "no such service")
      elsif entry.disabled?
        answer(reply_to, false, name, "disabled; use svc enable")
      elsif entry.obj.nil?
        answer(reply_to, false, name, "never loaded")
      elsif entry.start(Machine.board_millis)
        Log.info("services: #{name} started again")
        # Its tick deadline is back; the sleep this request arrived in has to
        # be recomputed or the first tick waits for the old one.
        request_early_update
        answer(reply_to, true, name, nil)
      else
        answer(reply_to, false, name, "already running")
      end
    else
      answer(reply_to, false, name, "unknown svc command: #{cmd}")
    end
    nil
  end

  # svc enable: switch it on for good and, if this is the first time it has
  # been wanted this boot, load it now.
  #
  # The load does NOT happen here. Loading a service means `require`, and
  # require compiles on a Sandbox task -- which needs the scheduler to run it,
  # and cannot get it while this task sits inside _spin. on_control IS inside
  # _spin (that is how control messages are delivered), so requiring from here
  # hangs the host outright: Sandbox#load_file joins with no timeout, and the
  # task it is waiting for never gets a turn. It works during on_create only
  # because that runs before the loop starts.
  #
  # So the switch and the record happen now, and the load, the start and the
  # answer happen on the next turn of the loop (finish_enable from on_update).
  def handle_enable(reply_to, name)
    entry = find_service(name)
    if entry.nil?
      answer(reply_to, false, name, "no such service")
      return nil
    end
    was_disabled = entry.enable
    record_state(entry.name, true)
    unless was_disabled
      # Already on. Recording it anyway is deliberate: it pins the choice
      # against a list that might be edited to disable it later.
      answer(reply_to, entry.running?, name, entry.running? ? nil : "enabled but not running")
      return nil
    end
    @pending_enable = entry
    @pending_enable_reply = reply_to
    request_early_update
    nil
  end

  # The other half of handle_enable, run from on_update where require is safe.
  def finish_enable(now)
    entry = @pending_enable
    return nil unless entry
    reply_to = @pending_enable_reply
    @pending_enable = nil
    @pending_enable_reply = nil
    if entry.obj.nil?
      unless load_service(entry, now)
        answer(reply_to, false, entry.name, "enabled but could not be loaded")
        return nil
      end
    end
    entry.start(now)
    Log.info("services: #{entry.name} enabled")
    answer(reply_to, true, entry.name, nil)
    nil
  end

  # svc disable: out of the delivery list and out of the next boot. The
  # instance is kept, so enabling it again does not reload the file.
  def handle_disable(reply_to, name)
    entry = find_service(name)
    if entry.nil?
      answer(reply_to, false, name, "no such service")
      return nil
    end
    if entry.disabled?
      record_state(entry.name, false)
      answer(reply_to, false, name, "already disabled")
      return nil
    end
    call_service(entry, 3, nil, nil) if entry.has_stop && entry.obj
    entry.disable
    record_state(entry.name, false)
    Log.info("services: #{entry.name} disabled")
    answer(reply_to, true, name, nil)
    nil
  end

  # One service per message, then a count.
  #
  # A message payload is 176 bytes (FMRB_MAX_MSG_PAYLOAD_SIZE), which three
  # services already overflow -- and going over raises, so the whole list in
  # one Hash is not a shape that works here at all. Per-service also suits
  # the reader: ps prints each line as it arrives instead of waiting for the
  # set, and the count at the end is what tells it the list is complete.
  def send_list(reply_to)
    return nil unless reply_to
    topic = reply_to.to_s
    i = 0
    while i < @services.size
      publish(topic, { "cmd" => "svc", "svc" => @services[i].to_report })
      i += 1
    end
    publish(topic, { "cmd" => "svc_end", "count" => @services.size })
    nil
  end

  def answer(reply_to, ok, name, err)
    return nil unless reply_to
    msg = { "cmd" => "svc_result", "ok" => ok, "name" => name.to_s }
    msg["err"] = err if err
    publish(reply_to.to_s, msg)
    nil
  end

  def find_service(name)
    return nil unless name
    i = 0
    while i < @services.size
      entry = @services[i]
      return entry if entry.name == name
      i += 1
    end
    nil
  end

  # Also the way a service takes itself out (ctx.stop_self).
  def stop_service(name, why)
    entry = find_service(name)
    return false unless entry
    return false unless entry.stop
    Log.info("services: #{name} stopped (#{why})")
    call_service(entry, 3, nil, nil) if entry.has_stop
    true
  end

  # ---- shutdown ------------------------------------------------------------

  def on_destroy
    i = 0
    while i < @services.size
      entry = @services[i]
      i += 1
      next unless entry.obj
      next unless entry.has_stop
      call_service(entry, 3, nil, nil)
    end
    i = 0
    while i < @topics.size
      unsubscribe(@topics[i])
      i += 1
    end
    unsubscribe(CTL_TOPIC)
    Log.info("services: host stopped")
    nil
  end
end

begin
  app = ServicesApp.new
  app.start
rescue => e
  Log.error("services: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
