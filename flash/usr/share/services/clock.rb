# System service: the machine's clock, as topics other services can act on.
#
# This is the pattern piece for a system service. It reads no hardware of its
# own -- the wallclock is already there -- and turns it into two topics, which
# is what lets a user's service (hourly_chime.rb) do something on the hour
# without knowing anything about time keeping.
#
#   clock/minute  every time the minute changes  {"year".."minute"}
#   clock/hour    at the top of the hour          same fields
#
# Checked once a minute, which is all the resolution these two topics have.
# Nothing is published while the clock is unset: the machine boots at the
# epoch when no RTC has answered yet, and a chime for 1970 helps nobody.
class ClockService
  # Below this the clock has not been set (the RTC is missing, empty, or has
  # not answered yet).
  EPOCH_YEAR = 2000

  def on_start(ctx)
    @ctx = ctx
    @last_minute = nil
    @last_hour = nil
    tick_clock
  end

  def on_tick(now_ms)
    tick_clock
  end

  def tick_clock
    wc = ::FmrbApp.wallclock
    return nil unless wc
    year = wc[:year].to_i
    return nil if year < EPOCH_YEAR

    minute = wc[:minute].to_i
    hour = wc[:hour].to_i
    return nil if @last_minute == minute

    first = @last_minute.nil?
    @last_minute = minute
    data = {
      "year" => year, "month" => wc[:month].to_i, "day" => wc[:day].to_i,
      "hour" => hour, "minute" => minute
    }
    @ctx.publish("clock/minute", data)

    # The hour is reported when the hour number changes, not when the minute
    # happens to be 0: a machine whose clock is set at 09:30 would otherwise
    # wait until 10:00 to notice it has an hour at all, and one that misses
    # the 0th minute (a long GC, a busy foreground app) would skip the hour
    # entirely. The first reading only records the hour, so setting the clock
    # does not ring anything.
    if @last_hour != hour
      @last_hour = hour
      @ctx.publish("clock/hour", data) unless first
    end
    nil
  end
end
