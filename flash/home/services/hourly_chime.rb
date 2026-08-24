# Rings a note on the hour.
#
# Two things worth copying are in here.
#
# It knows nothing about clocks. The system service
# /usr/share/services/clock.rb watches the wallclock and publishes
# "clock/hour"; this one subscribes to it. That is how services work together
# -- through Pub/Sub, never by calling each other.
#
# And it has a shape in time without a sleep. A handler must return at once,
# so the note is started in on_event and ended 200 ms later by asking for a
# single wake-up (ctx.wake_in) that arrives as on_wake. There is no
# interval_ms here at all: nothing has to be checked between chimes, so the
# host is left alone for the other 59 minutes 59.8 seconds.
#
#   [hourly_chime]
#   file = "hourly_chime.rb"
#   class = "HourlyChimeService"
class HourlyChimeService
  # The host reads this before it calls anything, so a service is subscribed
  # from the moment it starts.
  SUBSCRIBE = ["clock/hour"]

  CH = 1          # FmrbAudio::CH_PULSE2 -- pulse 1 is where music usually is
  NOTE = 659      # E5
  VOLUME = 8
  RING_MS = 200

  def on_start(ctx)
    @ctx = ctx
    @audio = ctx.audio
    @sounding = false
  end

  def on_event(topic, data)
    return nil unless topic == "clock/hour"
    hour = data ? data["hour"] : nil
    @ctx.log("chime for #{hour}:00")
    @audio.note_on(CH, NOTE, VOLUME)
    @sounding = true
    # One pending wake per service, and asking again replaces it -- so two
    # hours arriving back to back cannot leave a note ringing.
    @ctx.wake_in(RING_MS)
    nil
  end

  def on_wake(now_ms)
    silence
  end

  def on_stop
    silence
  end

  def silence
    return nil unless @sounding
    @audio.note_off(CH)
    @sounding = false
    nil
  end
end
