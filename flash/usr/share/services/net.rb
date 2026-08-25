# System service: the network, as a topic other services can act on.
#
# It answers the question that costs the most time in practice -- "what is
# this machine's address today?" -- by writing the IP into the log the moment
# there is one, and by publishing the change so nothing else has to poll.
#
#   net/state   on every change, and once at start
#               {"connected" => bool, "ip" => "...", "ssid" => "..."}
#   net/get     ask for the current state; the answer is another net/state
#
# Both, because a subscriber has two different problems. One that is running
# before the change hears it (that is the publish); one that starts later has
# missed it, and Pub/Sub keeps nothing, so it asks (that is net/get). The same
# shape as svc/ctl list, minus the reply_to -- the answer is the ordinary
# topic, since a late subscriber and a live one want the identical message.
class NetWatch
  SUBSCRIBE = ["net/get"]

  TOPIC = "net/state"

  def on_start(ctx)
    @ctx = ctx
    @connected = nil          # nil = nothing published yet
    publish_state(check_now)
  end

  # Cheap by design: FmrbApp.wifi_connected? allocates nothing, and the full
  # wifi_info -- which builds a Hash of three Strings -- is only read when the
  # answer has actually changed. At a 5 second tick that is the difference
  # between a few objects a day and a few thousand.
  def on_tick(now_ms)
    now = check_now
    return nil if now == @connected
    publish_state(now)
    nil
  end

  def on_event(topic, data)
    return nil unless topic == "net/get"
    publish_state(@connected.nil? ? check_now : @connected)
    nil
  end

  def check_now
    ::FmrbApp.wifi_connected? ? true : false
  end

  def publish_state(connected)
    changed = @connected != connected
    @connected = connected
    info = connected ? ::FmrbApp.wifi_info : nil
    ip = info ? info[:ip].to_s : ""
    ssid = info ? info[:ssid].to_s : ""
    if changed
      if connected
        @ctx.log("up: #{ip}#{ssid.empty? ? "" : " (#{ssid})"}")
      else
        @ctx.log("down")
      end
    end
    @ctx.publish(TOPIC, { "connected" => connected, "ip" => ip, "ssid" => ssid })
    nil
  end
end
