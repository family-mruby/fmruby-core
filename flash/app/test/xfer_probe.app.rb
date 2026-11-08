# Transfer probe (doc/asterism_file_transfer T0/T1): Asterism::Transfer
# between two machines with faults put in on purpose, and the numbers of
# T0 (the largest call, pieces and speed, the cost of SHA-256 and CRC32).
#
# The same code as the transfer app (Asterism::Transfer from
# /usr/share/asterism/transfer/), under an app of its own: it exposes
# <node>/xfer_probe/inbox, a receiver of the kind "probe" (the file is
# placed at /tmp/<name>.probe), and sends what its job file says. It takes
# files only from the nodes in /home/asterism/receive.toml (as the transfer
# app does).
#
# The job: /tmp/xfer_probe.job (JSON; tools/regress/transfer.rb puts it
# there), else /home/xfer_probe.job (a board, where the remote file tools
# reach /home), read once at the start. Every key may be left out:
#
#   {"keep_ms": 3000, "recv_piece": 3072, "recv_window": 4,
#    "recv_dir": "/home/xp/"}              the receiver: how long a file
#                                          that stopped coming is kept, the
#                                          largest piece and the pieces at
#                                          once it names to a sender, where
#                                          it writes (default /tmp/; a
#                                          file larger than /tmp's room
#                                          goes to /home)
#   {"to": "node-b", "path": "/tmp/x.bin", "name": "x",
#    "faults": "drop" | "twice" | "swap", "window": 1, "piece": 3072,
#    "sha256": "<64 hex>", "pace_ms": 10, "app": "transfer", "kind": "file",
#    "make": 4194304, "overwrite": true}   send a file (piece, window: the
#                                          most this sender goes to, else
#                                          what the receiver names; sha256:
#                                          declare a wrong one, for the
#                                          refusal; pace_ms: a step of the
#                                          sender at most that often, so
#                                          that a fault can be put in on the
#                                          way; app, kind: another receiver
#                                          than a probe, e.g. the transfer
#                                          app's kind "file"; make: write
#                                          that many bytes at path first)
#   {"bench": "sizes", "to": ..., "sizes": [...]}   one call of each size
#   {"bench": "speed", "to": ..., "path": ..., "pieces": [...],
#    "windows": [...], "make": n}          the file at each piece size and
#                                          number of pieces on their way
#                                          (the receiver names the most)
#   {"api": {"to": "node-b", "path": "/home/x.txt", "overwrite": true,
#    "name": "y.txt", "sd": true, "cancel_at": 65536}}
#                                          send a file through the
#                                          transfer app as an app does
#                                          (FmrbTransfer, the kind "file";
#                                          cancel_at: cancel it once that
#                                          much is there)
#   {"bench": "hash", "path": ..., "make": 262144}
#                                          SHA-256 (each engine there is)
#                                          and CRC32 of the file (make: write
#                                          that many bytes there first, for
#                                          a board with no file to hand)
#
# Faults (each piece position once, the first time it would be sent):
# drop leaves out every 10th piece (the 4th, 14th, ...), twice sends every
# 5th piece two times, swap sends every 7th piece after the one that
# follows it.
#
# Who may send probes: receive.toml's [probe] from, or its top-level from.
#
# The log (what the tool reads): "xfer_probe: ready as <node>",
# "xfer_probe: held N" (every 64 KB the receiver holds), "xfer_probe: done
# ok ..." / "xfer_probe: done failed: ...", "xfer_probe: received ...",
# "xfer_probe: bench ...", "xfer_probe: api now <state> <sent>/<size>" and
# "xfer_probe: api done ok <name>" / "xfer_probe: api done failed: ...".
require "/usr/share/asterism/transfer/transfer"

class ProbeInbox < ::Asterism::Transfer::Inbox
  RECEIVE_CONF = "/home/asterism/receive.toml"

  def refused(kind, from, meta)
    return "xfer_probe takes no #{kind}" unless kind == "probe"
    return nil unless File.exist?(RECEIVE_CONF)
    conf = FmrbToml.load(RECEIVE_CONF)
    allowed = []
    if conf.is_a?(Hash)
      allowed = conf["from"].to_a
      allowed += conf["probe"]["from"].to_a if conf["probe"].is_a?(Hash)
    end
    return nil if allowed.include?("*") || allowed.include?(from.to_s)
    "xfer_probe takes nothing from #{from}"
  end

  def received(t)
    dest = "#{dir}#{t['name']}.probe"
    File.delete(dest) if File.exist?(dest)
    File.rename(t["part"], dest)
    Log.info("xfer_probe: received #{dest} (#{t['size']} bytes, sha256 #{t['sha256'][0, 16]}) from #{t['from']}")
    { "status" => "placed", "path" => dest }
  end

  # A call of n bytes (bench sizes): its length back.
  def size(bytes, from: nil)
    bytes.to_s.bytesize
  end
end

class ProbeSender < ::Asterism::Transfer::Sender
  attr_accessor :faults

  def fault(pos, n)
    @seen ||= {}
    return nil if @seen[pos]
    @seen[pos] = true
    i = pos / @piece
    f = @faults
    return :drop if f == "drop" && i % 10 == 3
    return :twice if f == "twice" && i % 5 == 1
    return :swap if f == "swap" && i % 7 == 2
    nil
  end
end

class XferProbeApp < FmrbApp
  APP = "xfer_probe"
  JOBS = ["/tmp/xfer_probe.job", "/home/xfer_probe.job"]
  HELD_EVERY = 65536

  def on_create
    @job = read_job
    dir = (@job["recv_dir"] || "/tmp/").to_s
    Dir.mkdir(dir.byteslice(0, dir.bytesize - 1)) unless Dir.exist?(dir.byteslice(0, dir.bytesize - 1))
    @inbox = ProbeInbox.new(dir, piece: (@job["recv_piece"] || ::Asterism::Transfer::PIECE_MAX).to_i,
                            window: (@job["recv_window"] || ::Asterism::Transfer::WINDOW).to_i,
                            keep_ms: (@job["keep_ms"] || ::Asterism::Transfer::Inbox::KEEP_MS).to_i)
    @inbox.resume_held
    @sender = nil
    @bench = nil
    @shown = 0
    @link = FmrbAsterism::Link.new(self, APP, tag: "xfer_probe")
    Log.info("xfer_probe: start (sha256 by #{::Asterism::Transfer::SHA256.engine})")
  end

  def link_up(link)
    @inbox.path = "#{link.node}/#{APP}/inbox"
    methods = {}
    ::Asterism::Transfer::Inbox::METHODS.each { |k, v| methods[k] = v }
    methods["size"] = 1
    ::Asterism.expose("inbox", @inbox, methods: methods, caller: :from, doc: "transfer probe")
    Log.info("xfer_probe: ready as #{link.node}")
    start_job unless @started
    @started = true
  end

  def on_update
    up = @link.update
    @inbox.tick
    step_sender if @sender
    step_bench if @bench && up
    step_api if @api
    (@sender || @bench || @inbox.busy?) ? 1 : 50
  end

  def on_control(msg)
    @api.on_control(msg) if @api
  end

  def on_destroy
    @inbox.close
    @sender.close if @sender
    @link.close
  end

  # ---- the job ----

  def read_job
    i = 0
    while i < JOBS.size
      path = JOBS[i]
      i += 1
      next unless File.exist?(path)
      begin
        h = JSON.parse(File.open(path, "r") { |f| f.read }.to_s)
        return h if h.is_a?(Hash)
      rescue => e
        Log.warn("xfer_probe: #{path}: #{e.message}")
      end
    end
    {}
  end

  def start_job
    j = @job
    b = j["bench"].to_s
    make_file(j["path"].to_s, j["make"]) if j["make"].is_a?(Integer)
    if b == "hash"
      bench_hash(j["path"].to_s)
    elsif b == "sizes" || b == "speed"
      @bench = { "kind" => b, "i" => 0, "fut" => nil, "runs" => [] }
      if b == "speed"
        pieces = j["pieces"].to_a
        windows = j["windows"].to_a
        pi = 0
        while pi < pieces.size
          wi = 0
          while wi < windows.size
            @bench["runs"] << [pieces[pi].to_i, windows[wi].to_i]
            wi += 1
          end
          pi += 1
        end
      end
    elsif j["api"].is_a?(Hash)
      start_api(j["api"])
    elsif j["to"]
      @sender = new_sender(j["path"].to_s, j["name"].to_s, (j["piece"] || ::Asterism::Transfer::PIECE_MAX).to_i,
                           (j["window"] || ::Asterism::Transfer::WINDOW_MAX).to_i, j["sha256"])
      @sender.faults = j["faults"].to_s
      Log.info("xfer_probe: sending #{j['path']} (#{@sender.size} bytes, sha256 #{@sender.sha256[0, 16]}) to " \
               "#{j['to']}, faults #{j['faults'] || 'none'}")
    end
  end

  def new_sender(path, name, piece, window, sha = nil)
    app = (@job["app"] || APP).to_s
    kind = (@job["kind"] || "probe").to_s
    meta = @job["overwrite"] == true ? { "overwrite" => true } : {}
    ProbeSender.new("#{@job['to']}/#{app}/inbox", kind, path, name: name, piece: piece, window: window,
                    sha256: sha, meta: meta)
  end

  def step_sender
    s = @sender
    pace = @job["pace_ms"]
    if pace.is_a?(Integer) && pace > 0
      now = Machine.board_millis
      return if @next_step && now < @next_step
      @next_step = now + pace
    end
    s.step
    while s.pos >= @shown + HELD_EVERY
      @shown += HELD_EVERY
      Log.info("xfer_probe: held #{@shown}")
    end
    return unless s.done?
    @sender = nil
    st = s.stats
    summary = "#{s.took_ms} ms, #{st['calls']} calls, #{st['pieces']} pieces of #{s.piece}, #{s.window} at a time, " \
              "#{st['again']} again, " \
              "#{st['retries']} retries, #{st['begins']} begins, went on from [#{st['resumed_at'].join(',')}]"
    if s.ok?
      Log.info("xfer_probe: done ok #{s.answer['status']} #{s.answer['reason'] || s.answer['path']}; #{summary}")
    else
      Log.warn("xfer_probe: done failed: #{s.reason}; #{summary}")
    end
  end

  # ---- an app's way (FmrbTransfer) ----

  def start_api(a)
    require "/usr/share/transfer/fmrb_transfer"
    @api = FmrbTransfer.new(self)
    @api_said = ""
    @api_rq = @api.send_file(a["to"].to_s, a["path"].to_s, overwrite: a["overwrite"] == true, name: a["name"],
                             sd: a["sd"] == true)
    @api_cancel_at = a["cancel_at"]
    Log.info("xfer_probe: api asked to send #{a['path']} to #{a['to']}")
  rescue => e
    @api = nil
    Log.warn("xfer_probe: api done failed: #{e.message}")
  end

  def step_api
    @api.update
    st = @api.status(@api_rq)
    said = "#{st['state']} #{st['sent']}/#{st['size']}"
    if said != @api_said
      @api_said = said
      Log.info("xfer_probe: api now #{said}")
    end
    c = @api_cancel_at
    if c.is_a?(Integer) && st["state"] == "sending" && st["sent"] >= c
      @api_cancel_at = nil
      Log.info("xfer_probe: api cancelling at #{st['sent']}")
      @api.cancel(@api_rq)
    end
    return unless @api.done?(@api_rq)
    if @api.ok?(@api_rq)
      Log.info("xfer_probe: api done ok #{st['name']}")
    else
      Log.warn("xfer_probe: api done failed: #{st['reason']}")
    end
    @api = nil
  end

  # ---- T0's numbers ----

  def bench_hash(path)
    size = File.size(path)
    t = Machine.board_millis
    h = ::Asterism::Transfer::SHA256.file(path)
    Log.info("xfer_probe: bench hash sha256 #{::Asterism::Transfer::SHA256.engine} #{size} bytes " \
             "#{Machine.board_millis - t} ms (#{h[0, 16]})")
    if ::Asterism::Transfer::SHA256.engine != :ruby
      t = Machine.board_millis
      d = ::Asterism::Transfer::SHA256.new(:ruby)
      read_blocks(path) { |b| d.update(b) }
      Log.info("xfer_probe: bench hash sha256 ruby #{size} bytes #{Machine.board_millis - t} ms (#{d.hexdigest[0, 16]})")
    end
    if Object.const_defined?(:CRC)
      t = Machine.board_millis
      c = 0
      read_blocks(path) { |b| c = CRC.crc32(b, c) }
      Log.info("xfer_probe: bench hash crc32 #{size} bytes #{Machine.board_millis - t} ms (#{c})")
    end
    t = Machine.board_millis
    n = 0
    read_blocks(path) { |b| n += b.bytesize }
    Log.info("xfer_probe: bench hash read #{n} bytes #{Machine.board_millis - t} ms")
    Log.info("xfer_probe: bench done")
  end

  # n bytes of a pseudo-random 4 KB block, again and again
  def make_file(path, n)
    block = ""
    x = 12345
    while block.bytesize < 4096
      x = (x * 1103515245 + 12345) & 0x7fffffff
      block << ((x >> 16) & 0xff).chr
    end
    File.open(path, "w") do |f|
      left = n
      while left > 0
        k = left < 4096 ? left : 4096
        f.write(k == 4096 ? block : block.byteslice(0, k))
        left -= k
      end
    end
    Log.info("xfer_probe: made #{path} (#{n} bytes)")
  end

  def read_blocks(path)
    File.open(path, "r") do |f|
      while (b = f.read(4096))
        break if b.empty?
        yield b
      end
    end
  end

  def step_bench
    b = @bench
    if b["kind"] == "sizes"
      sizes = @job["sizes"].to_a
      if b["fut"].nil?
        return bench_done if b["i"] >= sizes.size
        n = sizes[b["i"]].to_i
        b["t"] = Machine.board_millis
        b["fut"] = ::Asterism["#{@job['to']}/#{APP}/inbox", timeout_ms: 5000].async.asterism_call("size", "x" * n)
        return
      end
      return unless b["fut"].done?
      n = sizes[b["i"]].to_i
      begin
        v = b["fut"].value
        Log.info("xfer_probe: bench call #{n} bytes ok (#{v}) #{Machine.board_millis - b['t']} ms")
      rescue => e
        Log.info("xfer_probe: bench call #{n} bytes failed #{Machine.board_millis - b['t']} ms: #{e.message}")
      end
      b["fut"] = nil
      b["i"] += 1
    else
      # one run after the other; each ends with its "done ok" line
      return if @sender
      return bench_done if b["i"] >= b["runs"].size
      run = b["runs"][b["i"]]
      b["i"] += 1
      @shown = 0
      @sender = new_sender(@job["path"].to_s, "bench", run[0], run[1])
      Log.info("xfer_probe: bench speed piece #{run[0]} window #{run[1]} (#{@sender.size} bytes)")
    end
  end

  def bench_done
    @bench = nil
    Log.info("xfer_probe: bench done")
  end
end

begin
  app = XferProbeApp.new
  app.start
rescue => e
  Log.error("xfer_probe: #{e.class}: #{e.message}")
end
