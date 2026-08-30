# System service: set the clock from the network, so nobody has to.
#
# Subscribes to net/state (published by net.rb) and asks an NTP server for the
# time once the machine is on the network, then again once a day. It is the
# service that removes a chore rather than adding a feature: without it every
# power-off means opening Set Clock by hand.
#
#   [timesync.config]
#   server = "pool.ntp.org"
#   interval_hours = 24
#
# SNTP and not NTP: one request, one reply, no round-trip correction and no
# discipline of the local clock. The error that leaves is well under a second,
# and everything on this machine that reads the clock -- the menu bar, the
# clock service, file timestamps -- is happy at that.
class TimeSync
  SUBSCRIBE = ["net/state"]

  DEFAULT_SERVER = "pool.ntp.org"
  NTP_PORT = 123

  # NTP counts from 1900, the machine counts from 1970.
  NTP_EPOCH_OFFSET = 2208988800

  # A reply is normally back in tens of milliseconds. Polled rather than
  # waited for: every handler here has to return at once, so the wait is a
  # chain of short wakes instead of a blocking receive.
  POLL_MS = 100
  MAX_POLLS = 20            # 2 seconds in total
  RETRY_MS = 1000           # pause between attempts

  # Three tries in a row, then stop and come back later.
  MAX_ATTEMPTS = 3

  # How long "later" is while the clock has never been set.
  #
  # Waiting for the next net/state was the first rule, and on hardware it left
  # the clock wrong for the whole session: the first boot after a flash fails
  # every time (the three attempts all land in the first ten seconds after the
  # association, before the network can answer a lookup), and net only
  # publishes on a CHANGE -- so a machine that comes up and stays connected
  # never gets another event to react to. A warm reboot succeeds, which is
  # what made this look fine until it was tried on a cold one.
  #
  # Once the clock has been set, this is not used again: the daily tick is
  # what keeps it honest, and a machine with the right time can afford to
  # wait.
  RETRY_LATER_MS = 300000   # 5 minutes

  # A plausible wall clock. Anything outside this is a malformed reply or a
  # server that has its own problems, and taking it would be worse than
  # keeping the time we have (the RTC driver applies the same test).
  MIN_YEAR = 2020
  MAX_YEAR = 2099
  # Seconds from 1970 to the start of each year. Cheaper and clearer than
  # date arithmetic for a range check.
  MIN_EPOCH = 1577836800    # 2020-01-01
  MAX_EPOCH = 4102444800    # 2100-01-01

  def on_start(ctx)
    @ctx = ctx
    cfg = ctx.config
    @server = (cfg["server"] || DEFAULT_SERVER).to_s
    hours = (cfg["interval_hours"] || 24).to_i
    hours = 24 if hours <= 0
    @interval_hours = hours
    @sock = nil
    @polls = 0
    @attempts = 0
    @synced = false
    @retry_pending = false
    @last_sync_ms = nil
    # net.rb publishes net/state when it starts, and the list starts it before
    # this one -- so that message was sent while this service did not yet
    # exist. net/get is the way back: it asks for the state to be published
    # again, and the answer arrives here as an ordinary net/state.
    ctx.publish("net/get", nil)
  end

  # net.rb publishes this on every change and at its own start, so a machine
  # that is already on the network when this service loads still gets one.
  def on_event(topic, data)
    return nil unless topic == "net/state"
    connected = data && data["connected"]
    unless connected
      close_socket
      @attempts = 0
      @retry_pending = false
      return nil
    end
    return nil if @synced
    @attempts = 0
    begin_request
    nil
  end

  # The periodic re-sync. interval_ms in the service list is the coarse timer
  # (a day); this only acts when that day has actually passed since the last
  # success, so a list configured with a shorter tick does not hammer anyone.
  def on_tick(now_ms)
    return nil unless @synced
    return nil if @last_sync_ms && now_ms - @last_sync_ms < @interval_hours * 3600000
    @attempts = 0
    begin_request
    nil
  end

  # The only place a wake lands, so it has both jobs: poll for the reply while
  # a request is outstanding, and start the next attempt when one is due.
  def on_wake(now_ms)
    if @retry_pending
      @retry_pending = false
      begin_request
      return nil
    end
    return nil unless @sock
    data = read_reply
    if data
      finish(data)
      return nil
    end
    return nil unless @sock     # read_reply hit an error and has moved on
    @polls += 1
    if @polls < MAX_POLLS
      @ctx.wake_in(POLL_MS)
      return nil
    end
    close_socket
    retry_or_give_up("no reply")
    nil
  end

  def on_stop
    close_socket
  end

  # ---- the exchange ------------------------------------------------------

  def begin_request
    close_socket
    @polls = 0
    @attempts += 1
    begin
      sock = ::UDPSocket.new
      sock.connect(@server, NTP_PORT)
      sock.send(self.class.build_request, 0)
      @sock = sock
      @ctx.wake_in(POLL_MS)
    rescue => e
      close_socket
      retry_or_give_up("#{e.class}: #{e.message}")
    end
    nil
  end

  def read_reply
    return nil unless @sock
    begin
      got = @sock.recvfrom_nonblock(64, 0)
      return nil unless got
      got[0]
    rescue => e
      close_socket
      retry_or_give_up("#{e.class}: #{e.message}")
      nil
    end
  end

  def finish(data)
    close_socket
    epoch = self.class.parse_reply(data)
    unless epoch
      retry_or_give_up("bad reply")
      return nil
    end
    @synced = true
    @attempts = 0
    @last_sync_ms = @ctx.now_ms
    apply(epoch)
    nil
  end

  # Never starts the next attempt straight away: it asks for a wake and lets
  # on_wake do it. Doing it here would recurse -- begin_request can fail and
  # call back into this method -- and would make three attempts take no time
  # at all, which is not a retry, it is the same failure three times.
  def retry_or_give_up(why)
    if @attempts < MAX_ATTEMPTS
      @ctx.log("#{@server}: #{why}; retrying")
      @retry_pending = true
      @ctx.wake_in(RETRY_MS)
      return nil
    end
    if @synced
      @ctx.log("#{@server}: #{why}; the clock is already set, waiting for the daily tick")
      return nil
    end
    @ctx.log("#{@server}: #{why}; trying again in #{RETRY_LATER_MS / 60000} minutes")
    @attempts = 0
    @retry_pending = true
    @ctx.wake_in(RETRY_LATER_MS)
    nil
  end

  def close_socket
    s = @sock
    @sock = nil
    return nil unless s
    begin
      s.close
    rescue
    end
    nil
  end

  # ---- applying it -------------------------------------------------------

  # On the device: the system clock and the RTC, so the time survives a power
  # cut. In the simulator: neither. Machine.set_hwclock is not implemented
  # there and raises NotImplementedError -- which is a ScriptError, NOT a
  # StandardError, so it would sail past every `rescue => e` in the service
  # host and take it down. The guard is the platform test, not a rescue.
  def apply(epoch)
    if ::FmrbConst::PLATFORM == "esp32"
      ::Machine.set_hwclock(epoch)
      @ctx.log("clock set from #{@server} (epoch #{epoch})")
      write_rtc(epoch)
    else
      @ctx.log("would set the clock to epoch #{epoch} (not applied off-device)")
    end
    nil
  end

  # Write the new time back to the RTC chip, so it survives a power cut. The
  # chip stores UTC, which the boot-time read hands back to the system clock
  # unchanged (rx8130.rb -> Machine.set_hwclock), and the fields are derived
  # from the epoch here rather than read back from the machine.
  #
  # Reading them back was the first attempt and it was wrong: FmrbApp.wallclock
  # returns LOCAL fields, so on a JST machine the RTC ended up nine hours
  # ahead, labelled UTC. The next boot then started nine hours in the future
  # until the sync corrected it. Caught on hardware -- the simulator writes no
  # RTC and could not have shown it. (Set Clock avoids the same trap by taking
  # the UTC hash that FmrbApp.set_wallclock hands back, which is the local ->
  # UTC conversion; here there is no local time involved at any point.)
  #
  # Retro carries an RX8900 and Modern an RX8130, but services are Modern
  # only, so this is the RX8130 alone.
  def write_rtc(epoch)
    # Not every Modern board has the chip. NARYA v4 has none -- its time comes
    # from here on every boot and nothing has to survive a power cut -- so
    # there is nothing to write and nothing to warn about.
    unless self.class.rtc_board?
      return nil
    end
    wc = self.class.utc_fields(epoch)
    i2c = nil
    begin
      i2c = ::I2C.new(unit: :ESP32_I2C1,
                      sda_pin: ::FmrbHw::PIN_I2C1_SDA,
                      scl_pin: ::FmrbHw::PIN_I2C1_SCL)
      rtc = ::RX8130.new(i2c)
      rtc.init
      rtc.write_time(wc)
      @ctx.log("RTC updated")
    rescue => e
      # Not fatal: the system clock is right either way, and the only loss is
      # that it will not survive a power cut.
      @ctx.log("RTC write failed: #{e.message}")
    ensure
      begin
        i2c.close if i2c
      rescue
      end
    end
    nil
  end

  # Whether this board has an RTC chip to write. Kept next to the writer so
  # the two cannot drift apart.
  def self.rtc_board?
    ::FmrbConst::BOARD != "naryav4"
  end

  # ---- the parts with no machine in them ---------------------------------
  #
  # Class methods on purpose: they are what the host tests exercise
  # (test/services/run.rb), and they touch nothing but Strings and Integers.

  # A client request: 48 bytes, all zero but the first. 0x1B is LI 0
  # (no warning), VN 3, Mode 3 (client). Built with setbyte because picoruby
  # has no Array#pack.
  def self.build_request
    pkt = "\x00" * 48
    pkt.setbyte(0, 0x1B)
    pkt
  end

  # The transmit timestamp: bytes 40..43, big-endian seconds since 1900.
  # Returns the epoch, or nil when the reply is too short or the time is not
  # believable.
  def self.parse_reply(data)
    return nil unless data
    return nil if data.bytesize < 48
    secs = 0
    i = 40
    while i < 44
      secs = (secs << 8) | data.getbyte(i)
      i += 1
    end
    return nil if secs == 0
    epoch = secs - NTP_EPOCH_OFFSET
    return nil unless valid_epoch?(epoch)
    epoch
  end

  def self.valid_epoch?(epoch)
    return false unless epoch.is_a?(Integer)
    epoch >= MIN_EPOCH && epoch < MAX_EPOCH
  end

  # Epoch seconds -> the UTC calendar fields the RTC wants. No time zone is
  # consulted anywhere: the epoch is UTC by definition and the chip stores
  # UTC, so bringing local time into it can only introduce the offset bug
  # this replaced.
  #
  # The date part is Howard Hinnant's civil-from-days, with the era shifted so
  # the year starts in March and the leap day lands at the end of it -- which
  # is what removes the special case for February.
  def self.utc_fields(epoch)
    days = epoch / 86400
    secs = epoch - days * 86400
    z = days + 719468                     # shift the epoch to 0000-03-01
    era = z / 146097                      # 400-year cycle
    doe = z - era * 146097                # day of era, 0..146096
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365
    y = yoe + era * 400
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100)
    mp = (5 * doy + 2) / 153              # month, March = 0
    d = doy - (153 * mp + 2) / 5 + 1
    m = mp < 10 ? mp + 3 : mp - 9
    y += 1 if m <= 2
    {
      year: y, month: m, day: d,
      hour: secs / 3600, minute: (secs % 3600) / 60, second: secs % 60
    }
  end
end
