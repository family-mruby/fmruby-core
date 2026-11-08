# Transfer app: files between machines over Asterism (doc/checkpoint F5,
# doc/asterism_file_transfer).
#
# The kernel cannot hold an Asterism session (on the standard configuration
# it is a Spinel program), so this headless app holds the machine's for
# moving apps. The kernel holds the steps of a move (D14,
# fmrb_kernel.rb start_move) and asks this app for the parts that talk to
# the other machine; this app answers the other machines' transfer apps.
#
# Receiving: the object <node>/transfer/inbox (D13), its calls named by the
# calling node (caller: :from) and taken only from the nodes this machine
# allows (/home/asterism/receive.toml, below). A file comes in pieces, by
# Asterism::Transfer (the asterism gem's data/transfer/transfer.rb, at
# /usr/share/asterism/transfer/ on the machine; its head says the calls):
#
#   check(kind, meta)        before anything is sent or stopped. For
#                            "checkpoint": meta {"names" => [...], "requires"
#                            => [...]}, the methods the program calls and
#                            the ones its .app.toml requires. Answers {"ok"
#                            => true, "missing" => [names this build lacks]},
#                            or {"ok" => false, "reason"} when a required one
#                            is missing. A long list comes in several calls.
#   begin(kind, meta)        meta {"name", "size", "sha256", "toml" (the
#                            attributes beside an image)} -> {"id", "piece",
#                            "pos" (where it goes on from)}
#   chunk(id, pos, bytes)    -> the position held here (a piece before it is
#                            read past, one after it is not taken)
#   commit(id)               all bytes are there and their sha256 matches:
#                            handed to the kind. A "checkpoint" image goes
#                            to /tmp/<name>.ckpt with its /tmp/<name>.toml
#                            and the kernel starts it -> {"status" =>
#                            "pending"}; {"status" => "refused", "reason"}
#                            when the sha256 does not match
#   status(id)               -> {"status" => "pending"} | {"status" =>
#                            "restored", "pid", "missing"} | {"status" =>
#                            "refused", "reason"}
#   cancel(id)               drop it; an image that came up is stopped
#
# One file at a time: a begin from another node while one is coming in is
# refused (busy) until no piece came for 10 s. Files are written beside
# their name (.part, with a note .xfer of who sends what) and appear only
# at the commit; a file that stopped coming is kept 60 s for its sender to
# go on (also across this app ending and starting again), then deleted.
# The kinds are a table (KINDS) so that more can be added (the wasm apps'
# install, doc/wasm_apps/plan.md 4).
#
# Who may send (/home/asterism/receive.toml; without it nobody may):
#
#   # The Asterism node IDs that may send apps to this machine
#   from = ["fmruby-abcdef", "wasm-123456"]
#
# "*" allows every node the router lets in. The caller's node ID is the one
# Asterism takes from the call; with a tls/ router and the console's
# certificates, the router's access control binds it to the certificate.
#
# Sending (asked by the kernel, APP_CONTROL):
#
#   move_check {to, calls}   the list /tmp/<name>.calls (requires on the
#                            first line, then a method name per line) to
#                            <to>/transfer/inbox check -> "move_checked"
#                            {ok, reason | missing}
#   move_send {to, path}     the image and its .toml by
#                            Asterism::Transfer::Sender (a lost piece or
#                            answer, a dropped connection: on from where the
#                            receiver stands), then status until it is up
#                            there -> "move_sent" {ok, pid (there), missing |
#                            reason}
#   move_cancel {to, pid}    the copy there is stopped (it came up too late)
#
# The connection is FmrbAsterism::Link's (the machine's settings); the
# kernel hears "transfer_ready" {node} once connected and "transfer_down"
# {reason} when it is not.
#
# .app.toml says movable = true: not to be moved (an Asterism session cannot
# be carried, so a request is refused), but so that its VM keeps the table
# of this build's method names (the baseline) that check answers from.

require "/usr/share/asterism/transfer/transfer"

# The receiving side: Asterism::Transfer::Inbox (begin / chunk / commit /
# cancel, resending from the receiver's position, the sha256 of the whole,
# the piece file kept in /tmp to go on after a lost connection or this app
# started again), with what this machine takes: the kinds, the senders
# (receive.toml), the checkpoint's check and status.
class TransferInbox < ::Asterism::Transfer::Inbox
  KINDS = ["checkpoint"]
  RECEIVE_CONF = "/home/asterism/receive.toml"
  RESTORE_MS = 8000
  TMP = "/tmp/"
  BUSY_FOR_MS = 2000

  def initialize
    super(TMP)
    @ckpt = nil       # the last committed checkpoint {id, from, path, at, result}
  end

  # A call came in a moment ago: the app polls often for the next one.
  def busy?
    super(BUSY_FOR_MS)
  end

  # ---- exposed (caller: :from), besides begin / chunk / commit / cancel ----

  def check(kind, meta, from: nil)
    @last_call = now_ms
    deny = refused(kind.to_s, from, meta)
    return { "ok" => false, "reason" => deny } if deny
    m = meta.is_a?(Hash) ? meta : {}
    req = list_of(m["requires"])
    i = 0
    while i < req.size
      if lacks?(req[i])
        return { "ok" => false,
                 "reason" => "#{node_name} has no method '#{req[i]}', which the app requires (requires in its .app.toml)" }
      end
      i += 1
    end
    names = list_of(m["names"])
    missing = []
    i = 0
    while i < names.size
      missing << names[i] if lacks?(names[i])
      i += 1
    end
    { "ok" => true, "missing" => missing }
  end

  def status(id, from: nil)
    @last_call = now_ms
    d = @ckpt
    raise ArgumentError, "no file #{id} from #{from}" unless d && d["id"] == id && d["from"] == from
    restore_result(d)
  end

  def cancel(id, from: nil)
    return true if super(id, from: from)
    d = @ckpt
    return false unless d && d["id"] == id && d["from"] == from
    r = restore_result(d)
    pid = r["pid"]
    if r["status"] == "restored" && pid
      Log.warn("transfer: #{from} cancels #{d['path']}; stopping PID #{pid}")
      @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
                        { "cmd" => "kill", "pid" => pid })
    end
    @ckpt = nil
    true
  end

  # ---- the hooks of Asterism::Transfer::Inbox ----

  attr_writer :app

  # Why this call is not taken (nil: taken).
  def refused(kind, from, meta)
    k = kind.to_s
    return "#{node_name} takes no #{k}" unless KINDS.include?(k)
    if k == "checkpoint" && !FmrbApp.respond_to?(:_checkpoint_restore)
      return "#{node_name} cannot start checkpoint images (this build has no mruby-checkpoint)"
    end
    allowed = list_of(allowed_senders)
    return nil if allowed.include?("*") || allowed.include?(from.to_s)
    "#{node_name} takes nothing from #{from} (from in #{RECEIVE_CONF})"
  end

  # The image is whole (its sha256 matches): it goes beside its attributes
  # under its name, and the kernel starts it (the answer is read by status).
  def received(t)
    path = "#{TMP}#{t['name']}.ckpt"
    toml = "#{TMP}#{t['name']}.toml"
    File.open(toml, "w") { |f| f.write(t["meta"]["toml"].to_s) }
    File.delete(path) if File.exist?(path)
    File.rename(t["part"], path)
    why = FmrbApp._checkpoint_restore(path)
    @ckpt = { "id" => t["id"], "from" => t["from"], "path" => path, "at" => now_ms, "result" => nil }
    if why
      @ckpt["result"] = { "status" => "refused", "reason" => why }
      return @ckpt["result"]
    end
    Log.info("transfer: #{path} from #{t['from']} (#{t['size']} bytes); starting it")
    { "status" => "pending" }
  end

  # ---- inside ----

  def node_name
    @app ? @app.node_name : "this machine"
  end

  def allowed_senders
    return [] unless File.exist?(RECEIVE_CONF)
    conf = FmrbToml.load(RECEIVE_CONF)
    conf ? conf["from"] : []
  rescue => e
    Log.warn("transfer: #{RECEIVE_CONF}: #{e.message}")
    []
  end

  def list_of(v)
    out = []
    return out unless v.is_a?(Array)
    i = 0
    while i < v.size
      out << v[i].to_s
      i += 1
    end
    out
  end

  def lacks?(name)
    FmrbApp._checkpoint_has_method(name) == false
  end

  def restore_result(d)
    return d["result"] if d["result"]
    st = FmrbApp._checkpoint_restore_poll
    if st[0] == "restored"
      missing = st[2].to_s.split(" ")
      d["result"] = { "status" => "restored", "pid" => st[1], "missing" => missing }
    elsif st[0] == "refused"
      d["result"] = { "status" => "refused", "reason" => st[2].to_s }
    elsif now_ms - d["at"] > RESTORE_MS
      d["result"] = { "status" => "refused", "reason" => "not started in #{RESTORE_MS} ms" }
    else
      return { "status" => "pending" }
    end
    Log.info("transfer: #{d['path']}: #{d['result']['status']}")
    d["result"]
  end
end

# The app. Sending is done a call at a time from on_update, each call left
# on its way (Asterism.call_async) while the app returns to its message
# wait: the native program schedules its tasks in turn, and a send that
# waited for its answers in one go would keep every other task (the
# kernel's clock for the hold among them) waiting until it was over.
class TransferApp < FmrbApp
  APP = "transfer"
  OBJECT = "inbox"
  # The names sent in one check call (a call carries 4 KB in all).
  CHECK_BYTES = 2048
  # A call waits this long for its answer: a slow machine on the other side
  # (the browser build polls its inbox between other tasks) may take seconds.
  CALL_TIMEOUT_MS = 10000
  STATUS_POLL_MS = 20
  STATUS_MS = 9000
  # what the kernel is told is cut to this (a message carries 176 bytes in
  # all, and only one of reason and missing is there)
  SAY_MAX = 112
  # on_update's wait while a job is under way, and otherwise
  BUSY_MS = 2
  IDLE_MS = 50

  def on_create
    @inbox = TransferInbox.new
    @inbox.app = self
    # a file that was coming in when this app last ended goes on
    @inbox.resume_held
    @jobs = []
    @job = nil        # the kernel's request under way (a Hash)
    @fut = nil        # its call on its way
    @last_sent = {}   # node => the id of the last file sent there
    @link = FmrbAsterism::Link.new(self, APP, tag: "transfer")
    Log.info("transfer: start (#{FmrbApp.pool_used} bytes of its pool used)")
  end

  def node_name
    @link.node
  end

  def link_up(link)
    ::Asterism.expose(OBJECT, @inbox,
                      methods: { "check" => 2, "begin" => 2, "chunk" => 3, "commit" => 1, "status" => 1,
                                 "cancel" => 1 },
                      caller: :from, doc: "files from other machines (doc/asterism_file_transfer)")
    tell_kernel({ "cmd" => "transfer_ready", "node" => link.node })
  end

  def link_down(link)
    tell_kernel({ "cmd" => "transfer_down", "reason" => cut(link.reason) })
  end

  def on_control(data)
    cmd = data["cmd"].to_s
    @jobs << data if cmd == "move_check" || cmd == "move_send" || cmd == "move_cancel"
    request_early_update
  end

  def on_update
    up = @link.update
    @inbox.tick
    if up
      start_job if @job.nil? && !@jobs.empty?
      step if @job
    end
    (@job || @inbox.busy?) ? BUSY_MS : IDLE_MS
  end

  def on_destroy
    @inbox.close
    @link.close
  end

  # ---- the kernel's requests ----

  def start_job
    job = @jobs.shift
    job["t0"] = Machine.board_millis
    job["n_calls"] = 0
    @job = job
    cmd = job["cmd"].to_s
    if cmd == "move_check"
      start_check(job)
    elsif cmd == "move_send"
      start_send(job)
    else
      id = @last_sent[job["to"].to_s]
      return finish(nil) unless id
      call("cancel", [id])
      job["step"] = "cancel"
    end
  end

  # One step of the request: its call has an answer.
  def step
    job = @job
    if job["sender"]
      sender = job["sender"]
      sender.step
      @last_sent[job["to"].to_s] = sender.id if sender.id
      sent(job, sender) if sender.done?
      return
    end
    if job["wait_until"]
      return if Machine.board_millis < job["wait_until"]
      job.delete("wait_until")
      call("status", [job["id"]])
      return
    end
    return if @fut.nil? || !@fut.done?
    v = nil
    begin
      v = @fut.value
    rescue ::Asterism::RemoteError => e
      return failed("#{job['to']}: #{e.remote_message}")
    rescue ::Asterism::Error => e
      return failed(unreached(job["to"].to_s, e))
    end
    @fut = nil
    s = job["step"]
    if s == "check" then checked(job, v)
    elsif s == "status" then started(job, v)
    elsif s == "cancel"
      Log.info("transfer: asked #{job['to']} to stop what it started")
      finish(nil)
    end
  end

  def call(method, args)
    @job["n_calls"] += 1
    p = ::Asterism["#{@job['to']}/#{APP}/#{OBJECT}", timeout_ms: CALL_TIMEOUT_MS].async
    @fut = p.asterism_call(method, *args)
  rescue ::Asterism::Error => e
    @fut = nil
    failed(unreached(@job["to"].to_s, e))
  end

  # ---- move_check: the list there, a batch at a time ----

  def start_check(job)
    text = read_file(job["calls"].to_s)
    return failed("cannot read #{job['calls']}") if text.nil?
    lines = text.split("\n")
    head = lines[0].to_s.split(" ")
    job["requires"] = head[1, head.size].to_a
    job["names"] = lines[1, lines.size].to_a
    job["next"] = 0
    job["missing"] = []
    job["step"] = "check"
    check_batch(job, job["requires"])
  end

  def check_batch(job, requires)
    names = job["names"]
    batch = []
    bytes = 0
    i = job["next"]
    while i < names.size && bytes < CHECK_BYTES
      batch << names[i]
      bytes += names[i].bytesize + 2
      i += 1
    end
    job["next"] = i
    call("check", ["checkpoint", { "names" => batch, "requires" => requires }])
  end

  def checked(job, v)
    r = v.is_a?(Hash) ? v : {}
    return failed(r["reason"].to_s) unless r["ok"]
    job["missing"].concat(r["missing"].to_a)
    return check_batch(job, []) if job["next"] < job["names"].size
    Log.info("transfer: #{job['to']} looked up #{job['names'].size} names in " \
             "#{Machine.board_millis - job['t0']} ms (#{job["n_calls"]} call(s)); it lacks #{job['missing'].size}")
    finish({ "cmd" => "move_checked", "ok" => true, "missing" => cut(job["missing"].join(" ")) })
  end

  # ---- move_send: the image (Asterism::Transfer::Sender: begin, the
  # pieces from where the receiver stands, commit), then status until it is
  # up there ----

  def start_send(job)
    path = job["path"].to_s
    stem = path.end_with?(".ckpt") ? path[0, path.size - 5] : path
    toml = read_file("#{stem}.toml")
    return failed("cannot read the image's .toml") if toml.nil?
    return failed("cannot read #{path}") unless File.exist?(path)
    job["size"] = File.size(path)
    name = stem[TransferInbox::TMP.size, stem.size].to_s
    job["sender"] = ::Asterism::Transfer::Sender.new("#{job['to']}/#{APP}/#{OBJECT}", "checkpoint", path,
                                                     name: name, meta: { "toml" => toml },
                                                     timeout_ms: CALL_TIMEOUT_MS)
    job["step"] = "send"
    job["sender"].step
  rescue => e
    failed("cannot send #{path}: #{e.message}")
  end

  # The sender is done: committed there (the kind's answer), or not.
  def sent(job, sender)
    job.delete("sender")
    job["id"] = sender.id
    st = sender.stats
    job["n_calls"] += st["calls"]
    job["carried"] = Machine.board_millis - job["t0"]
    job["committed"] = Machine.board_millis if sender.committing?
    if st["again"] > 0 || st["retries"] > 0 || st["begins"] > 1
      Log.info("transfer: #{job['path']} to #{job['to']}: #{st['again']} piece(s) again, #{st['retries']} retr(ies), " \
               "went on from #{st['resumed_at'].join(', ')}")
    end
    return failed("#{job['to']}: #{sender.reason}") unless sender.ok?
    started(job, sender.answer)
  end

  def started(job, v)
    r = v.is_a?(Hash) ? v : {}
    st = r["status"].to_s
    if st == "pending"
      if Machine.board_millis - job["committed"] > STATUS_MS
        return failed("#{job['to']} did not start it in #{STATUS_MS} ms")
      end
      job["step"] = "status"
      job["wait_until"] = Machine.board_millis + STATUS_POLL_MS
      return
    end
    took = Machine.board_millis - job["t0"]
    Log.info("transfer: #{job['path']} (#{job['size']} bytes) to #{job['to']}: carried in #{job['carried']} ms, " \
             "#{st} there after #{took} ms (#{job["n_calls"]} calls)")
    if st == "restored"
      finish({ "cmd" => "move_sent", "ok" => true, "pid" => r["pid"].to_i, "missing" => cut(r["missing"].to_a.join(" ")) })
    else
      failed(r["reason"].to_s)
    end
  end

  # ---- the end of a request ----

  def failed(why)
    job = @job
    Log.warn("transfer: #{job['cmd']} to #{job['to']}: #{why}")
    cmd = job["cmd"].to_s
    if cmd == "move_check"
      finish({ "cmd" => "move_checked", "ok" => false, "reason" => cut(why) })
    elsif cmd == "move_send"
      # Once committed it may still come up there: it is stopped there, so
      # that it does not go on in two places (the app goes on here).
      @jobs.unshift({ "cmd" => "move_cancel", "to" => job["to"] }) if job["committed"]
      finish({ "cmd" => "move_sent", "ok" => false, "pid" => -1, "reason" => cut(why) })
    else
      finish(nil)
    end
  end

  def finish(answer)
    @job["sender"].close if @job && @job["sender"]
    @job = nil
    @fut = nil
    tell_kernel(answer) if answer
    nil
  end

  # ---- helpers ----

  # Why the other machine was not reached (no transfer app there, no answer
  # in time, the connection lost), naming it.
  def unreached(to, e)
    msg = e.message.to_s
    msg.include?(to) ? msg : "#{to}: #{msg}"
  end

  def tell_kernel(h)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, h)
  end

  def cut(s)
    s = s.to_s
    s.bytesize > SAY_MAX ? s.byteslice(0, SAY_MAX).to_s : s
  end

  def read_file(path)
    return nil unless File.exist?(path)
    File.open(path, "r") { |f| f.read }
  rescue
    nil
  end
end

begin
  app = TransferApp.new
  app.start
rescue => e
  Log.error("transfer: #{e.class}: #{e.message}")
end
