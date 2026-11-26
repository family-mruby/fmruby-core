# TransferInbox: the transfer app's receiving side (transfer.app.rb, whose
# head says the calls and the rules). Kept in its own file so that the app's
# script stays well under the loader's 64 KB (main/app/fmrb_app.c
# MAX_SCRIPT_FILE_SIZE); read once by the app at its start.

require "/usr/share/asterism/transfer/transfer"
require "/usr/share/transfer/receive_conf"
require "/usr/share/transfer/app_package"

# The receiving side: Asterism::Transfer::Inbox (begin / chunk / commit /
# cancel, resending from the receiver's position, the sha256 of the whole,
# the piece file and its note kept to go on after a lost connection or this
# app started again), with what this machine takes: the kinds, the senders
# (receive.toml), where the pieces are written, how many may come at once,
# the checkpoint's check and status.
class TransferInbox < ::Asterism::Transfer::Inbox
  KINDS = ["checkpoint", "file", "app"]
  # An app to install (doc/wasm_apps W6): the owner has this long to answer
  # the question (the sender's wait is longer), and the question's app
  ASK_MS = 60000
  ASK_APP = "/usr/share/transfer/install_ask.app.rb"
  ASK_TICKET = "/tmp/.install_ask"
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
    @inst = nil       # the last app to install {id, from, name, state, ...}
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
    a = @inst
    return install_result(a) if a && a["id"] == id && a["from"] == from
    d = @ckpt
    raise ArgumentError, "no file #{id} from #{from}" unless d && d["id"] == id && d["from"] == from
    restore_result(d)
  end

  def cancel(id, from: nil)
    return true if super(id, from: from)
    a = @inst
    if a && a["id"] == id && a["from"] == from
      install_end(a, { "status" => "refused", "reason" => "cancelled by #{from}" }) if a["result"].nil?
      return true
    end
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
    return inbox_dir(meta) if k == "file"
    return app_refused(from, meta) if k == "app"
    nil
  end

  # An app to install: its name is the file's (meta "app" the same), it is
  # not installed here unless the sender asks to overwrite, and no other is
  # being installed.
  def app_refused(from, meta)
    m = meta.is_a?(Hash) ? meta : {}
    name = m["name"].to_s
    return "not an app's name: #{name} (letters, digits, _ and -)" unless ::FmrbAppPackage.name_ok?(name)
    a = @inst
    if a && a["result"].nil? && !(a["from"] == from && a["name"] == name)
      return "#{node_name} is installing another app (#{a['name']}); try again later"
    end
    if ::FmrbAppPackage.installed?(name) && m["overwrite"] != true
      return "#{name} is installed on #{node_name} already (send with overwrite to replace it)"
    end
    root = ::FmrbAppPackage::ROOT
    Dir.mkdir(root) unless Dir.exist?(root)
    nil
  rescue => e
    "cannot make #{::FmrbAppPackage::ROOT} (#{e.message})"
  end

  # A file's name may be any name that stays in /home/inbox/; an image's is
  # a plain one (letters, digits, _ and -).
  def name_ok?(kind, name)
    return ::Asterism::Transfer.file_name?(name) if kind == "file"
    return ::FmrbAppPackage.name_ok?(name) if kind == "app"
    ::Asterism::Transfer.plain_name?(name)
  end

  # What can still be written where the file is written: /tmp for an
  # image, the inbox the sender chose for a "file" (its pieces go there
  # straight). nil when not known (this build has no FmrbApp.free_bytes,
  # or the place cannot tell).
  def room(kind, meta)
    return nil unless FmrbApp.respond_to?(:free_bytes)
    if kind == "app"
      # the package and the app unpacked from it are there at once
      left = FmrbApp.free_bytes(::FmrbAppPackage::ROOT)
      return left.nil? ? nil : left / 2
    end
    FmrbApp.free_bytes(kind == "file" ? inbox_of(meta) : TMP)
  end

  # A "file"'s pieces go beside where it goes, hidden (a file sent here
  # never starts with "."): the commit renames it, nothing is copied.
  def part_path(kind, name, meta)
    return "#{::FmrbAppPackage::ROOT}/.#{name}.app.part" if kind == "app"
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
    most = kind == "checkpoint" ? WINDOW_IMAGE : WINDOW_MOST
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
    return take_app(t) if t["kind"] == "app"
    place_image(t)
  end

  # ---- an app to install (doc/wasm_apps W6) ----

  # The package is whole (its sha256 matches): it is unpacked and looked at
  # from the app's update (install_step), and the sender asks status.
  def take_app(t)
    a = @inst
    if a && a["result"].nil?
      remove(t["part"])
      return { "status" => "refused", "reason" => "#{node_name} is installing another app (#{a['name']})" }
    end
    @inst = { "id" => t["id"], "from" => t["from"], "name" => t["name"], "part" => t["part"],
              "meta" => t["meta"], "size" => t["size"], "state" => "unpack", "at" => now_ms, "result" => nil }
    Log.info("transfer: the app #{t['name']} from #{t['from']} (#{t['size']} bytes) came; looking at it")
    { "status" => "pending" }
  end

  def install_result(a)
    a["result"] || { "status" => "pending" }
  end

  # The app being installed, nil when none.
  def installing
    a = @inst
    a && a["result"].nil? ? a : nil
  end

  # A step of the install from the app's update: unpack and look, then
  # place or ask (and wait for the answer).
  def install_step
    a = installing
    return nil unless a
    if a["state"] == "unpack"
      install_unpack(a)
    elsif a["state"] == "asking"
      if now_ms - a["asked_at"] > ASK_MS
        install_end(a, { "status" => "refused", "reason" => "not answered on #{node_name} in #{ASK_MS / 1000} s" })
      end
    end
    nil
  end

  def install_unpack(a)
    stage = ::FmrbAppPackage.stage_of(a["name"])
    ::FmrbAppPackage.remove_tree(stage)
    got = ::FmrbAppPackage.unpack(a["part"], stage)
    a["stage"] = stage
    remove(a["part"])
    info = ::FmrbAppPackage.check(stage, a["name"], got["files"])
    a["info"] = info
    a["replaces"] = ::FmrbAppPackage.installed?(a["name"])
    mode = ::FmrbReceiveConf.install_mode
    certified = @app ? @app.certified? : false
    how = ::FmrbAppPackage.install_how(mode, certified, @app ? @app.can_ask? : false)
    why = ""
    why = " (auto, but the sender is not checked by a certificate)" if mode == "auto" && how != "auto"
    Log.info("transfer: the app #{a['name']} from #{a['from']}: #{info['lang']}, #{info['files']} file(s), " \
             "#{info['bytes']} bytes#{info['sdk'].empty? ? '' : ", SDK #{info['sdk']}"}; install: #{how}#{why}")
    if how == "auto"
      install_place(a)
    elsif how == "ask"
      install_ask(a)
    else
      install_end(a, { "status" => "refused", "reason" => "#{node_name}: #{::FmrbAppPackage::NO_SCREEN}" })
    end
  rescue ArgumentError => e
    remove(a["part"])
    ::FmrbAppPackage.remove_tree(a["stage"]) if a["stage"]
    install_end(a, { "status" => "refused", "reason" => "#{node_name} does not install #{a['name']}: #{e.message}" })
  end

  def install_place(a)
    path = ::FmrbAppPackage.place(a["stage"], a["info"])
    a["stage"] = nil
    Log.info("transfer: installed #{path} from #{a['from']}#{a['replaces'] ? ' (it replaces the one there)' : ''}")
    @app.installed(a["name"]) if @app
    install_end(a, { "status" => "installed", "name" => a["name"], "path" => path, "lang" => a["info"]["lang"] })
  rescue ArgumentError => e
    ::FmrbAppPackage.remove_tree(a["stage"]) if a["stage"]
    install_end(a, { "status" => "refused", "reason" => e.message })
  end

  # The owner is asked (the question's app with a ticket: who sent what).
  def install_ask(a)
    info = a["info"]
    lines = [["id", a["id"]], ["from", a["from"]], ["name", a["name"]], ["label", info["label"]],
             ["lang", info["lang"]], ["bytes", info["bytes"]], ["files", info["files"]], ["sdk", info["sdk"]],
             ["replaces", a["replaces"] ? "1" : "0"]]
    text = ""
    i = 0
    while i < lines.size
      v = flat(lines[i][1].to_s)
      text << "#{lines[i][0]}\t#{v}\n"
      i += 1
    end
    File.open(ASK_TICKET, "w") { |f| f.write(text) }
    a["state"] = "asking"
    a["asked_at"] = now_ms
    a["ask_pid"] = nil
    if @app
      @app.ask_owner(a)
    else
      install_end(a, { "status" => "refused", "reason" => "nobody to ask on #{node_name}" })
    end
  end

  # A value of the ticket on one line (tabs and line ends as spaces).
  def flat(s)
    out = ""
    i = 0
    while i < s.bytesize
      c = s.getbyte(i)
      out << ((c == 9 || c == 10 || c == 13) ? " " : s.byteslice(i, 1).to_s)
      i += 1
    end
    out
  end

  # The owner's answer (yes: install it).
  def install_answer(yes)
    a = installing
    return unless a && a["state"] == "asking"
    # the question's app ends by itself once it has answered
    a["ask_pid"] = nil
    if yes
      Log.info("transfer: the owner installs #{a['name']} from #{a['from']}")
      install_place(a)
    else
      install_end(a, { "status" => "refused", "reason" => "declined on #{node_name}" })
    end
  end

  # The question's app is gone: an answer that did not come is a no.
  def ask_gone(pid)
    a = installing
    return unless a && a["state"] == "asking" && a["ask_pid"] == pid
    a["ask_pid"] = nil
    install_end(a, { "status" => "refused", "reason" => "declined on #{node_name} (the question was closed)" })
  end

  def install_end(a, result)
    ::FmrbAppPackage.remove_tree(a["stage"]) if a["stage"]
    a["stage"] = nil
    remove(a["part"]) if a["part"]
    a["result"] = result
    # the question's app stopped if it is still up (no answer in time, the
    # sender cancelled); the transfer app forgets the question either way
    @app.ask_close(a) if @app
    File.delete(ASK_TICKET) if File.exist?(ASK_TICKET)
    Log.info("transfer: the app #{a['name']} from #{a['from']}: #{result['status']}" \
             "#{result['reason'] ? " (#{result['reason']})" : ''}")
  rescue => e
    Log.warn("transfer: #{e.message}")
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
