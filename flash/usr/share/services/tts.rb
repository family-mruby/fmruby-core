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
# With no PC to run VOICEVOX on, an OpenAI key does the same job over the
# network, and the machine needs nothing but WiFi:
#
#   [tts.config]
#   api_key = "sk-..."                # plain text; see the note below
#   cloud_model = "gpt-4o-mini-tts"
#   cloud_voice = "alloy"
#   cloud_timeout_ms = 10000
#
# The key is stored as it is written, on a filesystem the development build
# serves over HTTP. That is the same bargain the rest of this machine makes;
# it is a personal device, not a shared one.
#
# The order is cache -> VOICEVOX (if server is set) -> cloud (if api_key is
# set). With both set, VOICEVOX answers or the sentence is not said: falling
# through to the cloud on every hiccup would spend money quietly.
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

# The HTTP client lives next door; see the note at the top of that file for
# why it is not in here.
require "/usr/share/services/tts_http"

class TtsService
  SUBSCRIBE = ["tts/say"]

  CACHE_DIR = "/home/voice/cache"
  DEFAULT_SPEAKER = 1
  DEFAULT_TIMEOUT_MS = 3000
  # Cloud synthesis takes seconds, not milliseconds, so it gets its own
  # (longer) budget rather than borrowing VOICEVOX's.
  DEFAULT_CLOUD_TIMEOUT_MS = 10000
  CLOUD_HOST = "api.openai.com"
  CLOUD_PORT = 443
  CLOUD_PATH = "/v1/audio/speech"
  DEFAULT_CLOUD_MODEL = "gpt-4o-mini-tts"
  DEFAULT_CLOUD_VOICE = "alloy"
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
    @api_key = cfg ? cfg["api_key"] : nil
    @api_key = nil if @api_key && @api_key.to_s.empty?
    @cloud_model = (cfg && cfg["cloud_model"]) ? cfg["cloud_model"].to_s : DEFAULT_CLOUD_MODEL
    @cloud_voice = (cfg && cfg["cloud_voice"]) ? cfg["cloud_voice"].to_s : DEFAULT_CLOUD_VOICE
    @cloud_timeout_ms = (cfg && cfg["cloud_timeout_ms"]) ?
      cfg["cloud_timeout_ms"].to_i : DEFAULT_CLOUD_TIMEOUT_MS
    # Only a simulator without a trust store has any business turning this
    # off; the device has esp_crt_bundle compiled in.
    @tls_verify = (cfg && cfg.key?("tls_verify")) ? cfg["tls_verify"] : true
    ensure_cache_dir
    warn_if_cache_large
    where = []
    where << "#{@host}:#{@port}" if @host
    where << "openai/#{@cloud_model}" if @api_key
    if where.empty?
      ctx.log("tts ready (cache only)")
    else
      ctx.log("tts ready (#{where.join(' -> ')})")
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

    # Cache first, then whichever synthesiser is configured. With no server
    # the cloud is asked directly, which is the shape of a machine that has no
    # PC to lean on.
    path = @host ? cache_path(text, speaker) : cloud_cache_path(text)
    if ::File.exist?(path)
      @ctx.log("tts: cache hit (#{text})")
      @ctx.audio.play_wav(path)
      return nil
    end

    wav = nil
    if @host
      wav = synthesize(text, speaker)
      # VOICEVOX is the preferred voice when it is there; falling through to
      # the cloud on every hiccup would spend money quietly. It answers or the
      # sentence is not said.
    elsif @api_key
      wav = synthesize_cloud(text)
    else
      @ctx.log("tts: not cached and nothing configured (#{text})")
      return nil
    end
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

  # OpenAI's /v1/audio/speech: one POST, JSON in, the audio itself back.
  # response_format "wav" gives PCM 16-bit that play_wav takes as it is --
  # which is the whole reason this vendor was picked over the ones that wrap
  # their audio in base64 or want a request signature.
  def synthesize_cloud(text)
    http = TtsHttp.new(@cloud_timeout_ms)
    http.tls_verify = @tls_verify
    body = "{\"model\":\"#{@cloud_model}\",\"voice\":\"#{@cloud_voice}\"," \
           "\"response_format\":\"wav\",\"input\":\"#{json_escape(text)}\"}"
    headers = { "Authorization" => "Bearer #{@api_key}" }

    t0 = ::Machine.board_millis
    res = http.post(CLOUD_HOST, CLOUD_PORT, CLOUD_PATH, body,
                    "application/json", true, headers)
    spent = ::Machine.board_millis - t0

    unless res
      @ctx.log("tts: cloud failed (#{http.error})")
      return nil
    end
    if res[0] == 401
      # Worth its own line: a wrong key looks exactly like a network problem
      # from the outside, and this is the difference between the two.
      @ctx.log("tts: cloud rejected the api_key (401)")
      return nil
    end
    unless res[0] == 200
      # The status alone does not say why. 429 is "rate limited" and "you have
      # no credits left" equally, and those want different actions, so the
      # start of the body goes in the line too.
      @ctx.log("tts: cloud returned #{res[0]}: #{trim(res[1])}")
      return nil
    end

    wav = res[1]
    unless wav.bytesize > 44 && wav.byteslice(0, 4) == "RIFF"
      @ctx.log("tts: cloud did not return a WAV (#{wav.bytesize} bytes)")
      return nil
    end
    @ctx.log("tts: cloud synthesised #{wav.bytesize} bytes in #{spent} ms")
    wav
  end

  # One log line's worth of a response body, on one line.
  def trim(body)
    out = ""
    i = 0
    n = body.bytesize
    n = 160 if n > 160
    while i < n
      b = body.getbyte(i)
      out << ((b < 32) ? " " : body.byteslice(i, 1))
      i += 1
    end
    out
  end

  # Enough JSON string escaping for a sentence. Control characters other than
  # the two below cannot appear in text worth reading aloud.
  def json_escape(text)
    out = ""
    i = 0
    n = text.bytesize
    while i < n
      b = text.getbyte(i)
      if b == 34        # "
        out << "\\\""
      elsif b == 92     # backslash
        out << "\\\\"
      elsif b == 10
        out << "\\n"
      elsif b == 13
        out << "\\r"
      elsif b < 32
        out << " "
      else
        out << text.byteslice(i, 1)
      end
      i += 1
    end
    out
  end

  # ---- cache --------------------------------------------------------------

  # server + speaker + text, so the same sentence in another voice, or from
  # another synthesiser, is a different file.
  def cache_path(text, speaker)
    "#{CACHE_DIR}/#{cache_key(text, speaker)}.wav"
  end

  # Cloud audio is keyed by vendor+model+voice instead of a server address, so
  # the same sentence in a VOICEVOX voice and an OpenAI one are two files.
  def cloud_cache_path(text)
    "#{CACHE_DIR}/#{cache_key_for("openai:#{@cloud_model}:#{@cloud_voice}", text)}.wav"
  end

  def cache_key(text, speaker)
    cache_key_for(@server.to_s, "#{speaker}|#{text}")
  end

  def cache_key_for(scope, text)
    seed = "#{scope}|#{text}"
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
