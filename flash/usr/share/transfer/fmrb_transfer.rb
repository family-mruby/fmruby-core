# FmrbTransfer: an app sends a file to another machine over Asterism
# (doc/asterism_file_transfer T2). The file goes as the kind "file": the
# other machine places it under its /home/inbox/ (or its SD card's
# /sd/inbox/, sd: true; a machine with no card refuses it) when receiving
# is on there and its receive.toml takes files from this machine
# (enabled = true and [file] from; doc/asterism_file_transfer T7).
#
#   require "/usr/share/transfer/fmrb_transfer"
#
#   @xfer = FmrbTransfer.new(self)                 # self: the FmrbApp
#   id = @xfer.send_file("fmruby-abcdef", "/home/notes.txt")
#   id = @xfer.send_file(node, path, overwrite: true, name: "other.txt")
#   id = @xfer.send_file(node, path, sd: true)     # to its /sd/inbox/
#
#   def on_control(msg)
#     return if @xfer.on_control(msg)               # true: it was one of ours
#     ...
#   end
#
#   def on_update
#     @xfer.update                                  # asks again, gives up
#     st = @xfer.status(id)  # {"state", "sent", "size", "name", "reason"}
#     ...
#   end
#
#   @xfer.cancel(id)          # stops it; the other machine drops what it has
#
# state: "asking" (the transfer app has not taken it yet), "queued" (it
# waits behind another), "sending", "done" (name: the name it was placed
# under there; "foo(1).txt" when a foo.txt was there and overwrite was not
# asked), "failed" (reason). sent / size: the bytes the other machine holds,
# of the whole.
#
# The sending is the transfer app's (/usr/share/transfer/transfer.app.rb,
# which holds the machine's Asterism connection): this asks it over Pub/Sub
# (TOPIC) and it answers this app by message (transfer_queued,
# transfer_progress, transfer_done). It is started when it is not running.
# A request is a message, which carries 176 bytes in all: the node and the
# path together have about 90 bytes (send_file raises ArgumentError past
# that).
class FmrbTransfer
  TOPIC = "transfer/ctl"
  APP_PATH = "/usr/share/transfer/transfer.app.rb"
  APP_NAME = "Transfer"
  # a request not taken yet is published again this often (the transfer app
  # may be starting, and Pub/Sub keeps nothing for an app not subscribed yet)
  ASK_EVERY_MS = 1000
  # and given up after this long
  ASK_FOR_MS = 20000
  # what the kernel forwards to the transfer app must fit a message
  MSG_MAX = 176

  def initialize(app)
    @app = app
    @reqs = {}
    @last_rq = 0
  end

  # Asks for path to be sent to the machine named to (its Asterism node ID).
  # -> the request's id (an Integer). overwrite: a file of that name there is
  # replaced (once the new one is whole); otherwise the new one is placed
  # under a name of its own. name: the name there (default: path's last part).
  # sd: to the SD card's inbox there (/sd/inbox/) instead of /home/inbox/.
  def send_file(to, path, overwrite: false, name: nil, sd: false)
    to = to.to_s
    path = path.to_s
    raise ArgumentError, "no node to send to" if to.empty?
    raise ArgumentError, "no such file: #{path}" unless File.exist?(path)
    raise ArgumentError, "not a file: #{path}" if File.directory?(path)
    rq = new_rq
    req = { "cmd" => "send", "rq" => rq, "to" => to, "path" => path }
    req["ow"] = true if overwrite
    req["sd"] = true if sd
    req["name"] = name.to_s if name
    fwd = { "cmd" => "topic_data", "topic" => TOPIC, "data" => req, "src_pid" => 255 }
    if MessagePack.pack(fwd).bytesize > MSG_MAX
      raise ArgumentError, "the node and the path are too long for a request (#{to}, #{path})"
    end
    @reqs[rq] = { "state" => "asking", "req" => req, "t0" => now, "at" => now, "sent" => 0,
                  "size" => File.size(path), "name" => nil, "reason" => nil }
    start_app
    @app.publish(TOPIC, req)
    rq
  end

  # Stops a request: the transfer app cancels it (the other machine drops
  # the piece file it holds). -> false when it is over already.
  def cancel(rq)
    r = @reqs[rq]
    return false unless r
    return false if r["state"] == "done" || r["state"] == "failed"
    @app.publish(TOPIC, { "cmd" => "cancel", "rq" => rq })
    if r["state"] == "asking"
      r["state"] = "failed"
      r["reason"] = "cancelled"
    end
    true
  end

  # Call from on_update: a request not taken is asked again, and given up.
  def update
    @reqs.each do |rq, r|
      next unless r["state"] == "asking"
      t = now
      if t - r["t0"] > ASK_FOR_MS
        r["state"] = "failed"
        r["reason"] = "the transfer app did not take it in #{ASK_FOR_MS / 1000} s"
      elsif t - r["at"] >= ASK_EVERY_MS
        r["at"] = t
        start_app
        @app.publish(TOPIC, r["req"])
      end
    end
    nil
  end

  # Call from on_control: true when msg was the transfer app's answer to one
  # of these requests (the app need not look at it).
  def on_control(msg)
    return false unless msg.is_a?(Hash)
    c = msg["cmd"].to_s
    return false unless c == "transfer_queued" || c == "transfer_progress" || c == "transfer_done"
    r = @reqs[msg["rq"]]
    return false unless r
    return true if r["state"] == "done" || r["state"] == "failed"
    if c == "transfer_queued"
      r["state"] = "queued" if r["state"] == "asking"
    elsif c == "transfer_progress"
      r["state"] = "sending"
      r["sent"] = msg["sent"].to_i
      r["size"] = msg["size"].to_i if msg["size"]
    else
      if msg["ok"]
        r["state"] = "done"
        r["name"] = msg["name"].to_s
        r["sent"] = r["size"]
      else
        r["state"] = "failed"
        r["reason"] = msg["reason"].to_s
      end
    end
    true
  end

  # {"state", "sent", "size", "name", "reason"}, nil for an id not known.
  def status(rq)
    r = @reqs[rq]
    return nil unless r
    { "state" => r["state"], "sent" => r["sent"], "size" => r["size"], "name" => r["name"], "reason" => r["reason"] }
  end

  def done?(rq)
    r = @reqs[rq]
    r.nil? || r["state"] == "done" || r["state"] == "failed"
  end

  def ok?(rq)
    r = @reqs[rq]
    !r.nil? && r["state"] == "done"
  end

  # Forgets a request that is over.
  def forget(rq)
    @reqs.delete(rq)
    nil
  end

  private

  def now
    Machine.board_millis
  end

  # Unique among this machine's requests for long enough: the transfer app
  # takes a request again only with a larger id from the same app.
  def new_rq
    rq = Machine.board_millis
    rq = @last_rq + 1 if rq <= @last_rq
    @last_rq = rq
    rq
  end

  # The transfer app is started when it is not running (the kernel takes it
  # as its own once it connects).
  def start_app
    return if transfer_running?
    return if @spawn_at && now - @spawn_at < ASK_FOR_MS
    @spawn_at = now
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
                      { "cmd" => "spawn", "app_name" => APP_PATH })
  end

  def transfer_running?
    procs = FmrbApp.ps
    return false unless procs
    i = 0
    while i < procs.size
      p = procs[i]
      return true if p[:name] == APP_NAME && p[:state] != FmrbConst::PROC_STATE_FREE
      i += 1
    end
    false
  end
end
