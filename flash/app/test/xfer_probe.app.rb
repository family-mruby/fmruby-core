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
#   {"keep_ms": 3000, "piece": 3072}       the receiver: how long a file
#                                          that stopped coming is kept, the
#                                          largest piece taken
#   {"to": "node-b", "path": "/tmp/x.bin", "name": "x",
#    "faults": "drop" | "twice" | "swap", "window": 1, "piece": 3072,
#    "sha256": "<64 hex>", "pace_ms": 10} send a file (sha256: declare a
#                                          wrong one, for the refusal;
#                                          pace_ms: a step of the sender at
#                                          most that often, so that a fault
#                                          can be put in on the way)
#   {"bench": "sizes", "to": ..., "sizes": [...]}   one call of each size
#   {"bench": "speed", "to": ..., "path": ..., "pieces": [...],
#    "windows": [...]}                     the file at each piece size and
#                                          number of pieces on their way
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
# The log (what the tool reads): "xfer_probe: ready as <node>",
# "xfer_probe: held N" (every 64 KB the receiver holds), "xfer_probe: done
# ok ..." / "xfer_probe: done failed: ...", "xfer_probe: received ...",
# "xfer_probe: bench ...".
require "/usr/share/asterism/transfer/transfer"

class ProbeInbox < ::Asterism::Transfer::Inbox
  RECEIVE_CONF = "/home/asterism/receive.toml"

  def refused(kind, from, meta)
    return "xfer_probe takes no #{kind}" unless kind == "probe"
    return nil unless File.exist?(RECEIVE_CONF)
    conf = FmrbToml.load(RECEIVE_CONF)
    allowed = conf ? conf["from"].to_a : []
    return nil if allowed.include?("*") || allowed.include?(from.to_s)
    "xfer_probe takes nothing from #{from}"
  end

  def received(t)
    dest = "/tmp/#{t['name']}.probe"
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
    @inbox = ProbeInbox.new("/tmp/", piece: (@job["recv_piece"] || ::Asterism::Transfer::PIECE).to_i,
                            keep_ms: (@job["keep_ms"] || ::Asterism::Transfer::Inbox::KEEP_MS).to_i)
    @inbox.resume_held
    @sender = nil
    @bench = nil
    @shown = 0
    @link = FmrbAsterism::Link.new(self, APP, tag: "xfer_probe")
    Log.info("xfer_probe: start (sha256 by #{::Asterism::Transfer::SHA256.engine})")
  end

  def link_up(link)
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
    (@sender || @bench || @inbox.busy?) ? 1 : 50
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
    if b == "hash"
      make_file(j["path"].to_s, j["make"]) if j["make"].is_a?(Integer)
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
    elsif j["to"]
      @sender = new_sender(j["path"].to_s, j["name"].to_s, (j["piece"] || ::Asterism::Transfer::PIECE).to_i,
                           (j["window"] || 1).to_i, j["sha256"])
      @sender.faults = j["faults"].to_s
      Log.info("xfer_probe: sending #{j['path']} (#{@sender.size} bytes, sha256 #{@sender.sha256[0, 16]}) to " \
               "#{j['to']}, faults #{j['faults'] || 'none'}")
    end
  end

  def new_sender(path, name, piece, window, sha = nil)
    ProbeSender.new("#{@job['to']}/#{APP}/inbox", "probe", path, name: name, piece: piece, window: window,
                    sha256: sha)
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
    summary = "#{s.took_ms} ms, #{st['calls']} calls, #{st['pieces']} pieces, #{st['again']} again, " \
              "#{st['retries']} retries, #{st['begins']} begins, went on from [#{st['resumed_at'].join(',')}]"
    if s.ok?
      Log.info("xfer_probe: done ok #{s.answer['status']} #{s.answer['reason'] || s.answer['path']}; #{summary}")
    else
      Log.warn("xfer_probe: done failed: #{s.reason}; #{summary}")
    end
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
