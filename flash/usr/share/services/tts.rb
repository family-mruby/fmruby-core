# System service: says things out loud.
#
# Other services and apps publish to "tts/say" and forget about it:
#
#   ctx.publish("tts/say", {"text" => "あさ 7 じです"})
#
# Turning that into sound has two halves. The cache is the important one: a
# sentence that has been said before is a file on disk, so it plays with no
# network at all, immediately, and works with the machine offline. The
# synthesiser is only how a sentence gets into the cache the first time.
#
#   [tts]
#   file = "tts.rb"
#   class = "TtsService"
#   enable = true
#
#   [tts.config]
#   server = "http://192.168.10.5:50021"   # VOICEVOX; omit for cache-only
#   speaker = 1
#   timeout_ms = 3000
#
# The cache key is server + speaker + text, so the same sentence in another
# voice is a different file. To run a machine offline, leave the server line
# in the config and let it be unreachable: a miss costs one refused connection
# and a log line, and everything already said still plays. Removing the line
# changes the key and the machine stops finding its own cache.
#
# What it deliberately does NOT do: retry, queue, or wait. A reading is only
# useful while it is timely, so a failure is one log line and silence, and a
# second request while one is in flight replaces the first.

# Just enough HTTP to POST and read one response. Not a general client: no
# redirects, no keep-alive, no chunked bodies -- VOICEVOX sends
# Content-Length, and asking it to close after the response keeps the read
# loop honest about where the body ends.
#
# Kept in this file rather than beside it: it is 80 lines that nothing else
# has asked for yet, and a service is one require.
class TtsHttp
  READ_CHUNK = 1460          # about one segment; bigger just waits longer
  MAX_BODY = 2 * 1024 * 1024 # same ceiling play_wav has

  def initialize(timeout_ms)
    @timeout_ms = timeout_ms
  end

  # Returns [status, body] or nil. Never raises: every failure is the caller's
  # to log and drop.
  def post(host, port, path, body, content_type)
    deadline = ::Machine.board_millis + @timeout_ms
    prev = ::TCPSocket.timeout_ms
    ::TCPSocket.timeout_ms = @timeout_ms
    sock = nil
    begin
      sock = ::TCPSocket.new(host, port)
      req = "POST #{path} HTTP/1.1\r\n" \
            "Host: #{host}:#{port}\r\n" \
            "Content-Type: #{content_type}\r\n" \
            "Content-Length: #{body.bytesize}\r\n" \
            "Connection: close\r\n\r\n"
      sock.write(req)
      sock.write(body) if body.bytesize > 0
      read_response(sock, deadline)
    rescue => e
      @error = "#{e.class}: #{e.message}"
      nil
    ensure
      begin
        sock.close if sock
      rescue
      end
      ::TCPSocket.timeout_ms = prev
    end
  end

  # Why the last attempt failed, for the one log line the caller writes.
  def error
    @error
  end

  def read_response(sock, deadline)
    buf = ""
    head_end = nil
    # Headers first. Each readpartial is bounded by the socket timeout; the
    # deadline bounds the whole exchange, so a server that dribbles one byte
    # at a time cannot hold the service host indefinitely.
    while true
      if ::Machine.board_millis > deadline
        @error = "timeout waiting for headers"
        return nil
      end
      begin
        chunk = sock.readpartial(READ_CHUNK)
      rescue ::EOFError
        @error = "connection closed before headers"
        return nil
      end
      break if chunk.nil?
      buf << chunk
      head_end = buf.index("\r\n\r\n")
      break if head_end
      if buf.bytesize > 16384
        @error = "headers too long"
        return nil
      end
    end
    unless head_end
      @error = "no headers"
      return nil
    end

    head = buf.byteslice(0, head_end)
    status = status_of(head)
    want = content_length(head)
    got = buf.byteslice(head_end + 4, buf.bytesize - head_end - 4) || ""

    if want && want > MAX_BODY
      @error = "response too large (#{want})"
      return nil
    end

    while want.nil? || got.bytesize < want
      if ::Machine.board_millis > deadline
        @error = "timeout after #{got.bytesize} bytes"
        return nil
      end
      begin
        chunk = sock.readpartial(READ_CHUNK)
      rescue ::EOFError
        # No Content-Length means "until close", which is a complete body.
        break if want.nil?
        @error = "truncated: #{got.bytesize} of #{want} bytes"
        return nil
      end
      break if chunk.nil?
      got << chunk
      if got.bytesize > MAX_BODY
        @error = "response too large"
        return nil
      end
    end

    [status, got]
  end

  # "HTTP/1.1 200 OK" -> 200. Picked apart by hand: picoruby has no Regexp,
  # and a status line is fixed-width up to the code.
  def status_of(head)
    return 0 unless head.start_with?("HTTP/1.")
    digits = head.byteslice(9, 3).to_s
    return 0 unless digits.bytesize == 3
    n = 0
    i = 0
    while i < 3
      b = digits.getbyte(i)
      return 0 if b < 48 || b > 57
      n = n * 10 + (b - 48)
      i += 1
    end
    n
  end

  def content_length(head)
    i = head.downcase.index("content-length:")
    return nil unless i
    pos = i + 15
    n = head.bytesize
    pos += 1 while pos < n && (head.getbyte(pos) == 32 || head.getbyte(pos) == 9)
    return nil if pos >= n
    b = head.getbyte(pos)
    return nil if b < 48 || b > 57
    value = 0
    while pos < n
      b = head.getbyte(pos)
      break if b < 48 || b > 57
      value = value * 10 + (b - 48)
      pos += 1
    end
    value
  end
end

class TtsService
  SUBSCRIBE = ["tts/say"]

  CACHE_DIR = "/home/voice/cache"
  DEFAULT_SPEAKER = 1
  DEFAULT_TIMEOUT_MS = 3000
  # Not enforced, just reported once: whoever filled the cache is the one who
  # knows which entries still matter. "rmr /home/voice/cache" empties it.
  CACHE_WARN_COUNT = 100

  def on_start(ctx)
    @ctx = ctx
    cfg = ctx.config
    @server = cfg ? cfg["server"] : nil
    @speaker = (cfg && cfg["speaker"]) ? cfg["speaker"].to_i : DEFAULT_SPEAKER
    @timeout_ms = (cfg && cfg["timeout_ms"]) ? cfg["timeout_ms"].to_i : DEFAULT_TIMEOUT_MS
    @host, @port = split_server(@server)
    ensure_cache_dir
    warn_if_cache_large
    if @host
      ctx.log("tts ready (#{@host}:#{@port}, speaker #{@speaker})")
    else
      ctx.log("tts ready (cache only)")
    end
    nil
  end

  def on_event(topic, data)
    return nil unless topic == "tts/say"
    text = data ? data["text"] : nil
    return nil if text.nil?
    text = text.to_s
    return nil if text.empty?

    speaker = (data && data["speaker"]) ? data["speaker"].to_i : @speaker
    path = cache_path(text, speaker)

    if ::File.exist?(path)
      @ctx.log("tts: cache hit (#{text})")
      @ctx.audio.play_wav(path)
      return nil
    end

    unless @host
      @ctx.log("tts: not cached and no server (#{text})")
      return nil
    end

    wav = synthesize(text, speaker)
    return nil unless wav

    # Written whole, then renamed. A half-file in the cache would be played
    # forever after -- the next request would find it and never ask again.
    return nil unless store(path, wav)
    @ctx.audio.play_wav(path)
    nil
  end

  # ---- synthesis ----------------------------------------------------------

  def synthesize(text, speaker)
    http = TtsHttp.new(@timeout_ms)
    q = "/audio_query?text=#{url_encode(text)}&speaker=#{speaker}"
    res = http.post(@host, @port, q, "", "application/json")
    unless res && res[0] == 200
      @ctx.log("tts: audio_query failed (#{res ? res[0] : http.error})")
      return nil
    end
    query_json = res[1]

    res = http.post(@host, @port, "/synthesis?speaker=#{speaker}",
                    query_json, "application/json")
    unless res && res[0] == 200
      @ctx.log("tts: synthesis failed (#{res ? res[0] : http.error})")
      return nil
    end
    wav = res[1]
    # Refuse here rather than let play_wav refuse later: a body that is not a
    # WAV must not reach the cache, or every later request replays the
    # mistake.
    unless wav.bytesize > 44 && wav.byteslice(0, 4) == "RIFF"
      @ctx.log("tts: server did not return a WAV (#{wav.bytesize} bytes)")
      return nil
    end
    wav
  end

  # ---- cache --------------------------------------------------------------

  # server + speaker + text, so the same sentence in another voice, or from
  # another synthesiser, is a different file.
  def cache_path(text, speaker)
    "#{CACHE_DIR}/#{cache_key(text, speaker)}.wav"
  end

  def cache_key(text, speaker)
    seed = "#{@server}|#{speaker}|#{text}"
    # djb2. MbedTLS::Digest exists but the gem is not in every build, and a
    # collision here means one sentence plays another's recording -- a cost
    # worth the odds, which for a machine holding tens of phrases are nil.
    h = 5381
    i = 0
    n = seed.bytesize
    while i < n
      h = ((h * 33) + seed.getbyte(i)) & 0xFFFFFFFF
      i += 1
    end
    # The length is in the name too: two strings that collide in the hash
    # almost never collide in length as well.
    "#{to_hex8(h)}#{to_hex8(n)}"
  end

  def to_hex8(v)
    digits = "0123456789abcdef"
    out = ""
    shift = 28
    while shift >= 0
      out << digits[(v >> shift) & 0xF]
      shift -= 4
    end
    out
  end

  def store(path, wav)
    tmp = "#{path}.part"
    f = nil
    begin
      f = ::File.open(tmp, "w")
      f.write(wav)
      f.close
      f = nil
      ::File.rename(tmp, path)
      true
    rescue => e
      @ctx.log("tts: cannot write cache: #{e.message}")
      begin
        f.close if f
        ::File.unlink(tmp)
      rescue
      end
      false
    end
  end

  def ensure_cache_dir
    ::Dir.mkdir("/home/voice") unless ::Dir.exist?("/home/voice")
    ::Dir.mkdir(CACHE_DIR) unless ::Dir.exist?(CACHE_DIR)
  rescue => e
    @ctx.log("tts: cannot make #{CACHE_DIR}: #{e.message}")
  end

  def warn_if_cache_large
    n = 0
    d = ::Dir.open(CACHE_DIR)
    while (name = d.read)
      next if name == "." || name == ".."
      n += 1
    end
    d.close
    if n > CACHE_WARN_COUNT
      @ctx.log("tts: #{n} files in #{CACHE_DIR} (rmr it if that is too many)")
    end
  rescue
    nil
  end

  # ---- bits and pieces ----------------------------------------------------

  # "http://host:port" -> ["host", port]. Only http; TLS is a later stage.
  def split_server(server)
    return [nil, nil] if server.nil?
    s = server.to_s
    return [nil, nil] if s.empty?
    s = s.byteslice(7, s.bytesize - 7) if s.start_with?("http://")
    slash = s.index("/")
    s = s.byteslice(0, slash) if slash
    colon = s.index(":")
    return [s, 80] unless colon
    host = s.byteslice(0, colon)
    port = s.byteslice(colon + 1, s.bytesize - colon - 1).to_i
    [host, port > 0 ? port : 80]
  end

  # Percent-encode for a query string. Bytes, not characters: the text is
  # UTF-8 and every non-ASCII byte has to go out escaped.
  def url_encode(text)
    out = ""
    digits = "0123456789ABCDEF"
    i = 0
    n = text.bytesize
    while i < n
      b = text.getbyte(i)
      if (b >= 48 && b <= 57) || (b >= 65 && b <= 90) || (b >= 97 && b <= 122) ||
         b == 45 || b == 46 || b == 95 || b == 126
        out << text.byteslice(i, 1)
      else
        out << "%" << digits[(b >> 4) & 0xF] << digits[b & 0xF]
      end
      i += 1
    end
    out
  end
end
