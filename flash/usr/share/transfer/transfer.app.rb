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
#                            "window", "pos" (where it goes on from)}: the
#                            piece is the largest a call from that node
#                            carries here, the window (pieces on their way
#                            at once) what this app's pool has room for
#                            (TransferInbox#window)
#   chunk(id, pos, bytes)    -> the position held here (a piece before it is
#                            read past, one after it is not taken)
#   commit(id)               all bytes are there and their sha256 matches:
#                            handed to the kind. A "checkpoint" image goes
#                            to /tmp/<name>.ckpt with its /tmp/<name>.toml
#                            and the kernel starts it -> {"status" =>
#                            "pending"}. A "file" goes to /home/inbox/
#                            (below) -> {"status" => "placed", "name",
#                            "path"}. {"status" => "refused", "reason"} when
#                            the sha256 does not match or it cannot be placed
#   status(id)               -> {"status" => "pending"} | {"status" =>
#                            "restored", "pid", "missing"} | {"status" =>
#                            "refused", "reason"}
#   cancel(id)               drop it; an image that came up is stopped
#
# One file at a time: a begin from another node while one is coming in is
# refused (busy) until no piece came for 10 s. A file is written to a piece
# file (.part) and appears under its name only at the commit; a note of who
# sends what (.xfer) is in /tmp. An image's piece file is in /tmp too; a
# "file"'s is a hidden file in /home/inbox/ (".<name>.file.part"), so that
# the commit only renames it, and a file may be as large as the storage
# has room for. A file that stopped coming is kept 60 s for its sender to
# go on (also across this app ending and starting again), then deleted;
# after a restart of the machine the note is gone, and the hidden piece
# files left in /home/inbox/ are deleted when this app starts.
# The kinds are a table (KINDS) so that more can be added (the wasm apps'
# install, doc/wasm_apps/plan.md 4). Before a byte is sent, begin refuses
# (with the reason) a kind this machine does not take, a sender it does not
# take that kind from, a name the kind does not take, and a file that does
# not fit where it goes (FmrbApp.free_bytes of /tmp for an image, of
# /home/inbox/ for a "file"; where that is not known, it is refused where
# it stops being written).
#
#   checkpoint   an app's image (doc/checkpoint F5): started here
#   file         any file: placed under /home/inbox/ (made when missing),
#                or under /sd/inbox/ when the sender chooses the SD card
#                (meta "inbox" => "sd"; a machine with no card to write to
#                refuses it at begin), under the name it is sent with (letters of any kind, no
#                "/", "\\" or "..", not starting with ".", up to 48
#                bytes). A file of that name there stays, and the new one
#                gets a name of its own ("foo(1).txt"), unless the sender
#                asks to overwrite (meta "overwrite" => true): then it is
#                replaced once the new one is whole and its sha256 matches.
#                The name it was placed under goes back to the sender.
#
# Whether this machine receives at all, and who may send what
# (/home/asterism/receive.toml, else the system's /etc/asterism/receive.toml,
# which is off; FmrbReceiveConf in /usr/share/transfer/receive_conf.rb).
# Nothing is taken unless the switch is on (T7; no line is off):
#
#   enabled = true
#
# then a table per kind; a kind with no table is not taken:
#
#   [checkpoint]
#   from = ["fmruby-abcdef", "wasm-123456"]   # may move apps here
#
#   [file]
#   from = ["fmruby-abcdef", "pc-desk"]       # may put files in /home/inbox
#
# The older form, a from at the top with no table, is read as the
# checkpoint's (it was written when apps were all that came):
#
#   from = ["fmruby-abcdef"]                  # = [checkpoint] from
#
# (with both, either list lets a node move apps here). "*" allows every
# node the router lets in, "fmruby-*" every node whose ID starts so.
#
# Below a mark, the file has a part the console writes (T6,
# /usr/share/transfer/receive_conf.rb): the senders chosen for this machine
# in Admin > Members, sent by the console's bridge (policy_set, below) and
# written only when they changed. A kind is taken from the senders of both
# parts; the console's part stays as last written while the console cannot
# be reached. The shell's asend -l shows both.
#
#   policy_set({"file" => [...], "checkpoint" => [...], "at"})
#                            from the console's node only (console in
#                            config.toml, "console" by default) and over a
#                            tls/ router only (the caller's ID is bound to
#                            its certificate there), or in the browser
#                            build on a page the console's door connected
#                            with its user's certificate (T8,
#                            /tmp/.asterism_door); refused otherwise ->
#                            {"ok", "written", "at", "receiving" (whether
#                            receiving is on here, T7)} The caller's node ID is the one Asterism takes
# from the call; with a tls/ router and the console's certificates, the
# router's access control binds it to the certificate.
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
#   move_stop {to}           the hold ran out while the image is on its way:
#                            given up (Sender#cancel, the receiver drops what
#                            it holds) unless it is committed there already
#                            (then it may still come up, and move_cancel
#                            stops it). Taken at once, not queued
#   move_cancel {to, pid}    the copy there is stopped (it came up too late)
#
# Sending a file (asked by an app over Pub/Sub, FmrbTransfer in
# /usr/share/transfer/fmrb_transfer.rb, which the shell's send uses):
#
#   topic "transfer/ctl" {"cmd" => "send", "rq", "to", "path", "ow"
#   (overwrite), "sd" (to the SD card's inbox there), "name"}
#                            the file as the kind "file" to <to>; the asking
#                            app hears "transfer_queued" {rq}, then
#                            "transfer_progress" {rq, sent, size} as it goes
#                            and "transfer_done" {rq, ok, name | reason}
#   {"cmd" => "cancel", "rq"}  stops it (the other machine drops its piece
#                            file); "transfer_done" {ok: false, reason
#                            "cancelled"}
#   {"cmd" => "receive_conf"}  receive.toml was changed (FmrbReceiveConf
#                            tell_transfer): read the switches again
#
# Receiving turned off while this app runs (T7; that message, or the file
# looked at every RECHECK_MS): new begins are refused at once (every call
# reads the file), and the app ends when it is idle: no request of its own
# under way or waiting, nothing coming in (a file whose pieces stopped for
# the inbox's STALE_MS counts as stopped; its piece file is kept as when
# the app ends any other way), and no call for QUIET_MS. A file already
# coming in is taken to its end. Started while receiving is off (to send a
# file or move an app), it stays as before: only a change from on to off
# ends it.
#
# Sending on (send_enabled = true, doc/window_menu M2): the app stays on the
# network while either switch is on (it ends, as above, once both are off)
# and keeps the list of the machines there (the peers: conn.nodes, the
# same source as the shell's anodes), each with its profile's name and host
# and whether it takes apps from here (its inbox's check, asked again every
# PEER_CHECK_MS). The window menu's Send to reads it without waiting:
#
#   {"cmd" => "peers_to", "pid", "for"} (from the kernel, when a menu opens)
#                            the list to the desktop at pid at once:
#                            "wm_peers" {for, n, seen, verified} and then n
#                            "wm_peer" {for, node, name, host, ok}. ok: 0 not
#                            asked yet, 1 takes apps from here, 2 receives
#                            nothing (no inbox, or receiving is off there), 3
#                            not from here (not in its list), 4 cannot start
#                            apps. verified: the router binds every node ID
#                            to its certificate (a tls/ router), so the names
#                            are checked ones
#
# One request at a time, with the kernel's, in the order they come.
#
# The connection is FmrbAsterism::Link's (the machine's settings); the
# kernel hears "transfer_ready" {node} once connected and "transfer_down"
# {reason} when it is not.
#
# .app.toml says movable = true: not to be moved (an Asterism session cannot
# be carried, so a request is refused), but so that its VM keeps the table
# of this build's method names (the baseline) that check answers from.

require "/usr/share/asterism/transfer/transfer"
require "/usr/share/transfer/receive_conf"

# The receiving side: Asterism::Transfer::Inbox (begin / chunk / commit /
# cancel, resending from the receiver's position, the sha256 of the whole,
# the piece file and its note kept to go on after a lost connection or this
# app started again), with what this machine takes: the kinds, the senders
# (receive.toml), where the pieces are written, how many may come at once,
# the checkpoint's check and status.
class TransferInbox < ::Asterism::Transfer::Inbox
  KINDS = ["checkpoint", "file"]
  INBOX = "/home/inbox/"
  # The SD card's inbox, which the sender chooses (meta "inbox" => "sd")
  SD = "/sd"
  SD_INBOX = "/sd/inbox/"
  RECEIVE_CONF = "/home/asterism/receive.toml"
  # the node whose senders' list is taken (policy_set), unless config.toml
  # names another (console = "...")
  DEFAULT_CONSOLE = "console"
  RESTORE_MS = 8000
  TMP = "/tmp/"
  BUSY_FOR_MS = 2000
  # The pieces on their way at once: what the pool has room for (a piece
  # is held about PIECE_COST times over while it is taken in: the call's
  # payload, its String, the answer, garbage until the next GC), at most
  # WINDOW_MOST; an image is tens of KB, a few pieces in all.
  PIECE_COST = 4
  WINDOW_MOST = 8
  WINDOW_IMAGE = 4

  def initialize
    super(TMP)
    @ckpt = nil       # the last committed checkpoint {id, from, path, at, result}
  end

  # A call came in a moment ago: the app polls often for the next one.
  def busy?(ms = BUSY_FOR_MS)
    super(ms)
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

  # The console's list of who may send here (T6): {"file" => [...],
  # "checkpoint" => [...], "at" => when it was changed there}, written to
  # the managed part of receive.toml (FmrbReceiveConf) when it is not the
  # one there. Taken only from the console (CONSOLE_KEY in config.toml,
  # "console" by default), and only over a connection with certificates
  # (a tls/ router): there the router's access control binds the caller's
  # node ID to its certificate. Over any other connection (a home router
  # without certificates, peer to peer) anybody could call as "console",
  # so the list is refused there.
  def policy_set(h, from: nil)
    @last_call = now_ms
    console = @app ? @app.console_node : DEFAULT_CONSOLE
    raise ArgumentError, "#{node_name} takes the senders' list from #{console} only, not #{from}" if from.to_s != console
    unless @app && @app.certified?
      raise ArgumentError, "#{node_name} takes no list from #{console} over a connection without certificates " \
                           "(its router is not tls/, nor a page the door checked)"
    end
    raise ArgumentError, "not a table" unless h.is_a?(Hash)
    at = h["at"].to_s
    raise ArgumentError, "at: not a time" unless ::FmrbReceiveConf.at_ok?(at)
    lists = {}
    kinds = ::FmrbReceiveConf::KINDS
    i = 0
    while i < kinds.size
      lists[kinds[i]] = ::FmrbReceiveConf.checked_list(h[kinds[i]], kinds[i])
      i += 1
    end
    written = ::FmrbReceiveConf.write_managed(from.to_s, at, lists)
    if written
      Log.info("transfer: the senders from #{from} (#{at}) written: file #{lists['file'].join(' ')}; " \
               "checkpoint #{lists['checkpoint'].join(' ')}")
    end
    # receiving (T7): whether this machine takes anything at all; the list
    # is kept either way, and the console's row says when it is off
    { "ok" => true, "written" => written, "at" => at, "receiving" => ::FmrbReceiveConf.enabled? }
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
    s = ::FmrbReceiveConf.senders(k)
    return "#{node_name} does not receive (receiving is off there: areceive on)" unless s["enabled"]
    hand = s["hand"]
    managed = s["managed"]
    if hand.nil? && managed.nil?
      return "#{node_name} takes no #{k} (no [#{k}] in #{RECEIVE_CONF}, nor from the console)"
    end
    unless ::FmrbReceiveConf.allows?(hand, from) || ::FmrbReceiveConf.allows?(managed, from)
      # (short: the sender's shell shows about 110 bytes of it)
      return "#{node_name} takes no #{k} from #{from} (#{k} from in #{RECEIVE_CONF})" if managed.nil?
      return "#{node_name} takes no #{k} from #{from} (not in receive.toml, by hand or from #{s['by']})"
    end
    k == "file" ? inbox_dir(meta) : nil
  end

  # A file's name may be any name that stays in /home/inbox/; an image's is
  # a plain one (letters, digits, _ and -).
  def name_ok?(kind, name)
    return ::Asterism::Transfer.file_name?(name) if kind == "file"
    ::Asterism::Transfer.plain_name?(name)
  end

  # What can still be written where the file is written: /tmp for an
  # image, the inbox the sender chose for a "file" (its pieces go there
  # straight). nil when not known (this build has no FmrbApp.free_bytes,
  # or the place cannot tell).
  def room(kind, meta)
    return nil unless FmrbApp.respond_to?(:free_bytes)
    FmrbApp.free_bytes(kind == "file" ? inbox_of(meta) : TMP)
  end

  # A "file"'s pieces go beside where it goes, hidden (a file sent here
  # never starts with "."): the commit renames it, nothing is copied.
  def part_path(kind, name, meta)
    return super unless kind == "file"
    "#{inbox_of(meta)}.#{name}.#{kind}.part"
  end

  # The inbox a "file" goes to: /sd/inbox/ when the sender chose the SD
  # card (meta "inbox" => "sd"), else /home/inbox/.
  def inbox_of(meta)
    meta.is_a?(Hash) && meta["inbox"] == "sd" ? SD_INBOX : INBOX
  end

  # How many pieces may be on their way at once: as many as the free part
  # of this app's pool holds PIECE_COST times over, 1 to WINDOW_MOST (an
  # image: WINDOW_IMAGE).
  def window(kind, meta)
    most = kind == "file" ? WINDOW_MOST : WINDOW_IMAGE
    used = FmrbApp.pool_used
    pct = FmrbApp.pool_usage
    return 1 unless used.is_a?(Integer) && pct.is_a?(Integer) && used > 0 && pct > 0
    free = used * 100 / pct - used
    w = free / (::Asterism::Transfer::PIECE_MAX * PIECE_COST)
    w = most if w > most
    w < 1 ? 1 : w
  end

  def received(t)
    return place_file(t) if t["kind"] == "file"
    place_image(t)
  end

  # A "file" is whole (its sha256 matches): its hidden piece file beside
  # where it goes is renamed to its name, or to a name of its own when that
  # one is taken and overwrite was not asked. An overwritten file is
  # replaced only now.
  def place_file(t)
    over = t["meta"]["overwrite"] == true
    part = t["part"]
    inbox = inbox_of(t["meta"])
    name = over ? t["name"] : ::Asterism::Transfer.free_name(inbox, t["name"])
    if name.nil?
      remove(part)
      return { "status" => "refused", "reason" => "no free name for #{t['name']} in #{inbox}" }
    end
    dest = "#{inbox}#{name}"
    begin
      File.delete(dest) if over && File.exist?(dest)
      File.rename(part, dest)
    rescue => e
      remove(part)
      Log.warn("transfer: #{dest} from #{t['from']}: #{e.message}")
      return { "status" => "refused", "reason" => "cannot place #{dest} (#{e.message})" }
    end
    Log.info("transfer: placed #{dest} (#{t['size']} bytes) from #{t['from']}#{over ? ' (overwrite)' : ''}")
    { "status" => "placed", "name" => name, "path" => dest }
  end

  # Hidden piece files in /home/inbox/ and /sd/inbox/ that no note speaks
  # for (the machine started again, and /tmp with the notes is new):
  # deleted when this app starts, after resume_held took up the one it goes
  # on with.
  def sweep_inbox
    sweep_dir(INBOX)
    sweep_dir(SD_INBOX)
  end

  def sweep_dir(inbox)
    dir = inbox.byteslice(0, inbox.bytesize - 1)
    return unless Dir.exist?(dir)
    t = current_file
    keep = t ? t["part"] : nil
    names = Dir.entries(dir)
    i = 0
    while i < names.size
      n = names[i].to_s
      i += 1
      next unless n.bytesize > 6 && n.getbyte(0) == 46 && n.end_with?(".part")
      path = "#{inbox}#{n}"
      next if path == keep
      Log.info("transfer: #{path} left from before; deleted")
      remove(path)
    end
  rescue => e
    Log.warn("transfer: #{inbox}: #{e.message}")
  end

  # The inbox the sender chose, made when missing -> nil, or why it cannot
  # be. The SD card's is there only on a machine with a card that can be
  # written: its free space is known (a device without a card cannot tell
  # it, the browser build has no card), /sd is not read-only (the native
  # program shows no card: /sd falls in its read-only distribution tree),
  # and /sd/inbox/ is there or can be made (the simulation's /sd is a
  # directory of its flash/ when there is one).
  def inbox_dir(meta)
    m = meta.is_a?(Hash) ? meta : {}
    place = m["inbox"]
    unless place.nil? || place == "home" || place == "sd"
      return "#{node_name} has no inbox #{place} (home or sd)"
    end
    inbox = inbox_of(m)
    dir = inbox.byteslice(0, inbox.bytesize - 1)
    if inbox == SD_INBOX
      no_card = "#{node_name} has no SD card to write to"
      return no_card unless FmrbApp.respond_to?(:free_bytes) && FmrbApp.free_bytes(SD)
      return no_card if FmrbApp.respond_to?(:path_read_only?) && FmrbApp.path_read_only?(SD)
      begin
        Dir.mkdir(dir) unless Dir.exist?(dir)
      rescue => e
        return "#{no_card} (#{SD_INBOX}: #{e.message})"
      end
      return nil
    end
    Dir.mkdir(dir) unless Dir.exist?(dir)
    nil
  rescue => e
    "cannot make #{inbox} (#{e.message})"
  end

  # The image is whole (its sha256 matches): it goes beside its attributes
  # under its name, and the kernel starts it (the answer is read by status).
  def place_image(t)
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
# on its way (conn.call_async) while the app returns to its message
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
  # The apps' requests to send a file (FmrbTransfer)
  CTL_TOPIC = "transfer/ctl"
  # the kernel's notice that an app ended: its pid may go to a new app, whose
  # request ids start again from 1 (FmrbTransfer counts them per app)
  DIED_TOPIC = "app/died"
  # an app hears how far its file has got at most this often
  PROGRESS_MS = 250
  # a file asked for while this machine is not connected waits this long
  UNLINKED_MS = 30000
  # receiving off: receive.toml looked at this often, and the app ends after
  # this long without a call (T7)
  RECHECK_MS = 10000
  QUIET_MS = 3000
  # sending on (doc/window_menu M2): the machines on the network looked at
  # this often, whether each takes apps from here asked again this often, at
  # most this many kept (the window menu shows them all on the Retro), and
  # how long a profile or a check may take
  PEERS_MS = 2000
  PEER_CHECK_MS = 30000
  PEER_MAX = 8
  PEER_ASK_MS = 4000
  # what a peer's entry carries to the desktop (a message is 176 bytes)
  PEER_NODE_MAX = 64
  PEER_NAME_MAX = 32
  PEER_HOST_MAX = 12

  def on_create
    @inbox = TransferInbox.new
    @inbox.app = self
    # a file that was coming in when this app last ended goes on
    @inbox.resume_held
    @inbox.sweep_inbox
    @jobs = []
    @asked = {}       # pid => the last request id taken from that app
    @job = nil        # the kernel's request under way (a Hash)
    @fut = nil        # its call on its way
    @last_sent = {}   # node => the id of the last file sent there
    @link = FmrbAsterism::Link.new(self, APP, tag: "transfer")
    p = ::FmrbReceiveConf.parts
    @receiving = ::FmrbReceiveConf.enabled?(p)
    @sending = ::FmrbReceiveConf.send_enabled?(p)
    @closing = false
    @close_why = ""
    @peers = []       # the machines on the network while sending is on
    @peers_at = 0     # when conn.nodes was last looked at (0: never)
    @checked_at = Machine.board_millis
    @job_end = 0
    subscribe(CTL_TOPIC)
    subscribe(DIED_TOPIC)
    Log.info("transfer: start (#{FmrbApp.pool_used} bytes of its pool used)")
  end

  def node_name
    @link.node
  end

  # The console's node ID (config.toml's console, else "console"), read on
  # every list it sends.
  def console_node
    c = FmrbToml.load(FmrbAsterism::CONFIG_FILE)
    v = c.is_a?(Hash) ? c["console"].to_s : ""
    v.empty? ? TransferInbox::DEFAULT_CONSOLE : v
  end

  # Connected over a tls/ router, with this machine's certificate (the
  # settings take no tls/ router without one). In the browser build: the
  # console's door connected this page with its user's certificate (T8,
  # asterism-console docs/g1_5.md), which the page says by writing this
  # node's ID to DOOR_FILE while every WebSocket it has says so
  # (web/main.js); the router binds the callers to their certificates
  # there, and refuses every transfer key to the shared connection.
  DOOR_FILE = "/tmp/.asterism_door"

  def certified?
    return false unless @link.connected?
    router = @link.router.to_s
    return true if router.start_with?("tls/")
    ::FmrbConst::BOARD == "wasm" && router.start_with?("ws/") && door_checked?
  end

  def door_checked?
    return false unless ::File.exist?(DOOR_FILE)
    ::File.open(DOOR_FILE, "r") { |io| io.read }.to_s.strip == @link.node.to_s
  rescue StandardError
    false
  end

  def link_up(link)
    @inbox.path = "#{link.node}/#{APP}/#{OBJECT}"
    link.conn.expose(OBJECT, @inbox,
                     methods: { "check" => 2, "begin" => 2, "chunk" => 3, "commit" => 1, "status" => 1,
                                 "cancel" => 1, "policy_set" => 1 },
                     caller: :from, doc: "files from other machines (doc/asterism_file_transfer)")
    tell_kernel({ "cmd" => "transfer_ready", "node" => link.node })
  end

  def link_down(link)
    tell_kernel({ "cmd" => "transfer_down", "reason" => cut(link.reason) })
  end

  def on_control(data)
    cmd = data["cmd"].to_s
    @jobs << data if cmd == "move_check" || cmd == "move_send" || cmd == "move_cancel"
    stop_move(data["to"].to_s) if cmd == "move_stop"
    if cmd == "peers_to"
      peers_to(data["pid"], data["for"])
    elsif cmd == "topic_data" && data["topic"] == CTL_TOPIC
      app_request(data["data"], data["src_pid"])
    elsif cmd == "topic_data" && data["topic"] == DIED_TOPIC
      app_ended(data["data"])
    end
    request_early_update
  end

  def on_update
    up = @link.update
    @inbox.tick
    now = Machine.board_millis
    if now - @checked_at >= RECHECK_MS
      @checked_at = now
      check_receiving
    end
    if @closing && idle?
      Log.info("transfer: #{@close_why}; the transfer app ends")
      stop
      return IDLE_MS
    end
    if up
      tick_peers(now) if @sending
      @unlinked_at = nil
      start_job if @job.nil? && !@jobs.empty?
      step if @job
    else
      give_up_unlinked
    end
    (@job || @inbox.busy?) ? BUSY_MS : IDLE_MS
  end

  def on_destroy
    @inbox.close
    @link.close
    unsubscribe(CTL_TOPIC)
    unsubscribe(DIED_TOPIC)
  end

  # ---- the apps' requests (FmrbTransfer) ----

  def app_request(req, pid)
    return unless req.is_a?(Hash)
    if req["cmd"].to_s == "receive_conf"
      check_receiving
      return
    end
    return unless pid.is_a?(Integer)
    rq = req["rq"]
    return unless rq.is_a?(Integer)
    c = req["cmd"].to_s
    if c == "send"
      # asked again (the answer had not come yet): taken once. The ids count
      # up from 1 in each app (FmrbTransfer), and @asked forgets a pid when
      # its app ends (app_ended), so a larger id is a new request.
      seen = @asked[pid]
      if seen.nil? || rq > seen
        @asked[pid] = rq
        @jobs << { "cmd" => "file_send", "to" => req["to"].to_s, "path" => req["path"].to_s,
                   "name" => req["name"], "overwrite" => req["ow"] == true, "sd" => req["sd"] == true,
                   "pid" => pid, "rq" => rq }
        Log.info("transfer: PID #{pid} asks to send #{req['path']} to #{req['to']}")
      end
      tell_app(pid, { "cmd" => "transfer_queued", "rq" => rq })
    elsif c == "cancel"
      cancel_request(pid, rq)
    end
  end

  # An app ended: forget the last id taken from its pid, so the next app
  # given that pid is heard from its first request on. Its jobs go on (a
  # file asked for is still sent; there is just no one to tell).
  def app_ended(d)
    return unless d.is_a?(Hash)
    @asked.delete(d["pid"])
  end

  def cancel_request(pid, rq)
    job = @job
    if job && job["cmd"] == "file_send" && job["pid"] == pid && job["rq"] == rq
      sender = job["sender"]
      if sender
        sender.cancel
      else
        failed("cancelled")
      end
      return
    end
    i = 0
    while i < @jobs.size
      j = @jobs[i]
      if j["cmd"] == "file_send" && j["pid"] == pid && j["rq"] == rq
        @jobs.delete_at(i)
        tell_app(pid, { "cmd" => "transfer_done", "rq" => rq, "ok" => false, "reason" => "cancelled" })
        return
      end
      i += 1
    end
  end

  # ---- receiving turned off (T7) ----

  # Reads the switches again: when the last of them goes off the app ends
  # once idle; on again before that, it stays.
  def check_receiving
    p = ::FmrbReceiveConf.parts
    on = ::FmrbReceiveConf.enabled?(p)
    snd = ::FmrbReceiveConf.send_enabled?(p)
    if @receiving && !on
      if snd
        Log.info("transfer: receiving is off; new files are refused (sending keeps the app on the network)")
      elsif !@closing
        Log.info("transfer: receiving is off; new files are refused, and the app ends when what is coming in is in")
        @closing = true
        @close_why = "receiving is off"
      end
    end
    if @sending && !snd
      @peers = []
      @peers_at = 0
      if on
        Log.info("transfer: sending is off (receiving keeps the app on the network)")
      elsif !@closing
        Log.info("transfer: sending is off; the app ends when what is coming in is in")
        @closing = true
        @close_why = "sending is off"
      end
    end
    Log.info("transfer: sending is on; keeping the machines on the network") if snd && !@sending
    if (on || snd) && @closing
      Log.info("transfer: #{on ? 'receiving' : 'sending'} is on again")
      @closing = false
    end
    @receiving = on
    @sending = snd
  end

  # ---- the machines to send to (doc/window_menu M2) ----

  # While sending is on and connected: the peers follow conn.nodes, a
  # new one is asked for its profile, and each is asked every PEER_CHECK_MS
  # whether it takes apps from here. Every question is on its way while the
  # app goes on (a Future, polled by the link's update); nothing waits here.
  def tick_peers(now)
    if @peers_at == 0 || now - @peers_at >= PEERS_MS
      @peers_at = now
      follow_nodes
    end
    i = 0
    while i < @peers.size
      peer_step(@peers[i], now)
      i += 1
    end
  end

  def follow_nodes
    c = @link.conn
    return unless c
    own = @link.node.to_s
    all = c.nodes
    kept = []
    i = 0
    while i < @peers.size
      kept << @peers[i] if all.include?(@peers[i]["node"])
      i += 1
    end
    i = 0
    while i < all.size && kept.size < PEER_MAX
      n = all[i].to_s
      i += 1
      next if n == own || n.bytesize > PEER_NODE_MAX || peer_of(kept, n)
      kept << { "node" => n, "name" => "", "host" => "", "ok" => 0, "pf" => nil, "cf" => nil,
                "asked" => 0, "profiled" => false }
    end
    @peers = kept
  end

  def peer_of(list, n)
    i = 0
    while i < list.size
      return list[i] if list[i]["node"] == n
      i += 1
    end
    nil
  end

  def peer_step(pe, now)
    n = pe["node"]
    unless pe["profiled"] || pe["pf"]
      begin
        pe["pf"] = net.profile_async(n, false, nil, PEER_ASK_MS)
      rescue ::Asterism::Error
        pe["profiled"] = true
      end
    end
    f = pe["pf"]
    if f && f.done?
      pe["pf"] = nil
      pe["profiled"] = true
      pr = nil
      begin
        pr = ::Asterism::Profile.check(f.raw_value)
      rescue ::Asterism::Error
        pr = nil
      end
      if pr.is_a?(Hash)
        pe["name"] = cut_to(pr["name"].to_s, PEER_NAME_MAX)
        h = pr["host"].to_s
        h = pr["kind"].to_s if h.empty?
        pe["host"] = cut_to(h, PEER_HOST_MAX)
      end
    end
    if pe["cf"].nil? && (pe["asked"] == 0 || now - pe["asked"] >= PEER_CHECK_MS)
      pe["asked"] = now
      begin
        px = net["#{n}/#{APP}/#{OBJECT}", timeout_ms: PEER_ASK_MS].async
        pe["cf"] = px.asterism_call("check", "checkpoint", { "names" => [], "requires" => [] })
      rescue ::Asterism::Error
        pe["ok"] = 2
      end
    end
    c = pe["cf"]
    if c && c.done?
      pe["cf"] = nil
      pe["ok"] = peer_answer(c)
    end
  end

  # What a check answered (see peers_to for the numbers).
  def peer_answer(c)
    v = c.value
    return 1 if v.is_a?(Hash) && v["ok"]
    why = v.is_a?(Hash) ? v["reason"].to_s : ""
    return 2 if why.include?("does not receive")
    return 4 if why.include?("cannot start")
    3
  rescue ::Asterism::RemoteError
    3
  rescue ::Asterism::Error
    # nobody answers there (no transfer app: it receives nothing), or no
    # answer in time
    2
  end

  # The list to the desktop, at once (the kernel asks when a window menu
  # with Send to opens).
  def peers_to(pid, for_pid)
    return unless pid.is_a?(Integer)
    check_receiving unless @sending
    tell_app(pid, { "cmd" => "wm_peers", "for" => for_pid, "n" => @peers.size,
                    "seen" => @peers_at > 0, "verified" => certified? })
    i = 0
    while i < @peers.size
      pe = @peers[i]
      tell_app(pid, { "cmd" => "wm_peer", "for" => for_pid, "node" => pe["node"], "name" => pe["name"],
                      "host" => pe["host"], "ok" => pe["ok"] })
      i += 1
    end
  end

  def cut_to(s, n)
    s.bytesize > n ? s.byteslice(0, n).to_s : s
  end

  # Nothing under way: no request of its own, nothing coming in (or a file
  # whose pieces stopped), no call for a moment.
  def idle?
    return false if @job || !@jobs.empty?
    now = Machine.board_millis
    return false if now - @job_end < QUIET_MS
    return false if @inbox.busy?(QUIET_MS)
    t = @inbox.current_file
    t.nil? || now - t["at"].to_i > ::Asterism::Transfer::Inbox::STALE_MS
  end

  # Files asked for while this machine is not connected are not kept
  # waiting forever.
  def give_up_unlinked
    return if @jobs.empty? && @job.nil?
    @unlinked_at ||= Machine.board_millis
    return if Machine.board_millis - @unlinked_at < UNLINKED_MS
    why = "not connected to Asterism (#{cut(@link.reason)})"
    kept = []
    i = 0
    while i < @jobs.size
      j = @jobs[i]
      if j["cmd"] == "file_send"
        tell_app(j["pid"], { "cmd" => "transfer_done", "rq" => j["rq"], "ok" => false, "reason" => why })
      else
        kept << j
      end
      i += 1
    end
    @jobs = kept
    @unlinked_at = Machine.board_millis
  end

  def tell_app(pid, h)
    send_message(pid, FmrbConst::MSG_TYPE_APP_CONTROL, h)
  rescue => e
    Log.warn("transfer: cannot tell PID #{pid}: #{e.message}")
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
    elsif cmd == "file_send"
      start_file_send(job)
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
      return file_step(job, sender) if job["cmd"] == "file_send"
      return give_up_send(job) if job["stop"] && !sender.done? && !sender.committing?
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
    p = net["#{@job['to']}/#{APP}/#{OBJECT}", timeout_ms: CALL_TIMEOUT_MS].async
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
                                                     net: net, name: name, meta: { "toml" => toml },
                                                     timeout_ms: CALL_TIMEOUT_MS)
    job["step"] = "send"
    job["sender"].step
  rescue => e
    failed("cannot send #{path}: #{e.message}")
  end

  # The kernel gave the move up (the hold ran out, the app goes on here): an
  # image still on its way is not sent on, and the receiver drops what it
  # holds of it. One committed there already (or waiting for status) is left
  # to come up; the kernel has it stopped then (move_cancel).
  def stop_move(to)
    i = 0
    while i < @jobs.size
      j = @jobs[i]
      if j["cmd"] == "move_send" && j["to"].to_s == to
        @jobs.delete_at(i)
        Log.info("transfer: #{j['path']} to #{to}: not sent (the hold ran out)")
        tell_kernel({ "cmd" => "move_sent", "ok" => false, "pid" => -1, "reason" => "the hold ran out" })
        return
      end
      i += 1
    end
    job = @job
    return unless job && job["cmd"] == "move_send" && job["to"].to_s == to
    job["stop"] = true
    give_up_send(job)
  end

  # A move_send the kernel gave up: once its begin is answered (so that the
  # receiver knows the id it drops), the sender cancels.
  def give_up_send(job)
    sender = job["sender"]
    return if sender.nil? || sender.committing? || sender.id.nil?
    Log.info("transfer: #{job['path']} to #{job['to']}: given up at #{sender.pos} of #{job['size']} bytes " \
             "(the hold ran out); dropped there")
    sender.cancel
    job.delete("sender")
    failed("the hold ran out")
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

  # ---- file_send: a file to another machine's /home/inbox/ (an app's
  # request) ----

  def start_file_send(job)
    path = job["path"]
    return failed("no such file: #{path}") unless File.exist?(path)
    name = job["name"] ? job["name"].to_s : path.split("/").last.to_s
    meta = {}
    meta["overwrite"] = true if job["overwrite"]
    meta["inbox"] = "sd" if job["sd"]
    job["size"] = File.size(path)
    job["told"] = -1
    job["told_at"] = 0
    job["sender"] = ::Asterism::Transfer::Sender.new("#{job['to']}/#{APP}/#{OBJECT}", "file", path,
                                                     net: net, name: name, meta: meta, timeout_ms: CALL_TIMEOUT_MS)
    job["step"] = "send"
    Log.info("transfer: sending #{path} (#{job['size']} bytes) to #{job['to']} as #{name}")
    job["sender"].step
  rescue => e
    failed("cannot send #{path}: #{e.message}")
  end

  def file_step(job, sender)
    pos = sender.pos
    now = Machine.board_millis
    if pos != job["told"] && (now - job["told_at"] >= PROGRESS_MS || sender.done?)
      job["told"] = pos
      job["told_at"] = now
      tell_app(job["pid"], { "cmd" => "transfer_progress", "rq" => job["rq"], "sent" => pos, "size" => job["size"] })
    end
    return unless sender.done?
    st = sender.stats
    took = now - job["t0"]
    if sender.ok? && sender.answer["status"].to_s == "placed"
      name = sender.answer["name"].to_s
      Log.info("transfer: #{job['path']} (#{job['size']} bytes) to #{job['to']}: placed as #{name} in #{took} ms " \
               "(#{st['calls']} calls, #{st['again']} again, #{st['retries']} retries)")
      tell_app(job["pid"], { "cmd" => "transfer_done", "rq" => job["rq"], "ok" => true, "name" => cut(name) })
      job.delete("sender")
      return finish(nil)
    end
    why = sender.ok? ? sender.answer["reason"].to_s : sender.reason.to_s
    job.delete("sender")
    failed(why == "cancelled" ? why : "#{job['to']}: #{why}")
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
    elsif cmd == "file_send"
      tell_app(job["pid"], { "cmd" => "transfer_done", "rq" => job["rq"], "ok" => false, "reason" => cut(why) })
      finish(nil)
    else
      finish(nil)
    end
  end

  def finish(answer)
    @job["sender"].close if @job && @job["sender"]
    @job = nil
    @job_end = Machine.board_millis
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

  # The connection (Asterism::Connection) the calls go out on; raises
  # Asterism::Disconnected while there is none.
  def net
    c = @link.conn
    raise ::Asterism::Disconnected, "not connected" unless c
    c
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
