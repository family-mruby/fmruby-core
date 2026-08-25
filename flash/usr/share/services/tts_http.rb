# The HTTP half of the tts service (see tts.rb).
#
# Split off for a reason worth knowing about: a service file is compiled on
# the device when the host requires it, and one file with all of this in it
# was too much -- the host died during the require with no exception to show
# for it. Two files compile one after the other and both fit. The limit is
# the amount of CODE, not the size of the file: stripping every comment from
# the combined version (down to 13 KB) still killed it.
#
# Just enough HTTP to POST and read one response. Not a general client: no
# redirects, no keep-alive. Bodies come either with a Content-Length (what
# VOICEVOX sends) or chunked (what a cloud API streaming its audio sends), and
# both are handled; asking the server to close afterwards keeps the read loop
# honest about where a body with neither ends.
#
# TLS is the same code with SSLSocket in place of TCPSocket: on the device the
# certificate bundle is already attached (esp_crt_bundle), so an https URL
# needs no CA of its own.

class TtsHttp
  READ_CHUNK = 1460          # about one segment; bigger just waits longer
  MAX_BODY = 2 * 1024 * 1024 # same ceiling play_wav has
  # Where the simulator's container keeps its trust store. The device does not
  # use this: its bundle is compiled in.
  POSIX_CA_FILE = "/etc/ssl/certs/ca-certificates.crt"

  def initialize(timeout_ms)
    @timeout_ms = timeout_ms
  end

  # Returns [status, body] or nil. Never raises: every failure is the caller's
  # to log and drop.
  #
  # tls: true opens an SSLSocket instead. headers is an optional Hash of extra
  # request headers (Authorization, for one).
  def post(host, port, path, body, content_type, tls = false, headers = nil)
    deadline = ::Machine.board_millis + @timeout_ms
    prev = ::TCPSocket.timeout_ms
    ::TCPSocket.timeout_ms = @timeout_ms
    sock = nil
    begin
      # The port is in the Host header only when it is not the default for the
      # scheme; some APIs reject the explicit :443.
      hostport = (tls ? port == 443 : port == 80) ? host : "#{host}:#{port}"
      req = "POST #{path} HTTP/1.1\r\n" \
            "Host: #{hostport}\r\n" \
            "Content-Type: #{content_type}\r\n" \
            "Content-Length: #{body.bytesize}\r\n" \
            "Connection: close\r\n"
      if headers
        headers.each { |k, v| req << "#{k}: #{v}\r\n" }
      end
      req << "\r\n"
      sock = tls ? open_tls(host, port) : ::TCPSocket.new(host, port)
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

  # An SSLSocket with the platform's own trust. On the device that is
  # esp_crt_bundle, attached by the port; on the simulator the container's CA
  # file has to be named, and there is nothing to name it after but the path.
  def open_tls(host, port)
    ctx = ::SSLContext.new
    if @tls_verify == false
      ctx.verify_mode = ::SSLContext::VERIFY_NONE
    elsif ::Machine.posix?
      # POSIX builds have no bundle compiled in; point at the system store.
      begin
        ctx.ca_file = POSIX_CA_FILE
      rescue => e
        @error = "no CA file at #{POSIX_CA_FILE}: #{e.message}"
      end
    end
    # open() sets the hostname, connects and handshakes in one go; new() would
    # want a TCPSocket to wrap and then a separate connect.
    ::SSLSocket.open(host, port, ctx)
  end

  # Whether to check the server's certificate. Only a simulator without a CA
  # store has any business turning it off.
  def tls_verify=(on)
    @tls_verify = on
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

    # A cloud API streaming its audio answers chunked instead of naming a
    # length. Read the raw stream to the end, then unwrap it.
    if want.nil? && chunked?(head)
      raw = read_until_close(sock, deadline, got)
      return nil unless raw
      body = dechunk(raw)
      return nil unless body
      return [status, body]
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

  def chunked?(head)
    i = head.downcase.index("transfer-encoding:")
    return false unless i
    line_end = head.index("\r\n", i) || head.bytesize
    head.byteslice(i, line_end - i).downcase.include?("chunked")
  end

  # Everything until the peer hangs up, starting from what is already read.
  def read_until_close(sock, deadline, got)
    while true
      if ::Machine.board_millis > deadline
        @error = "timeout after #{got.bytesize} bytes"
        return nil
      end
      begin
        chunk = sock.readpartial(READ_CHUNK)
      rescue ::EOFError
        return got
      end
      break if chunk.nil?
      got << chunk
      if got.bytesize > MAX_BODY
        @error = "response too large"
        return nil
      end
    end
    got
  end

  # Unwrap chunked transfer coding: a hex length line, that many bytes, CRLF,
  # repeated, ending with a zero length. Trailers after it are ignored.
  # Written by hand for the same reason as the status line -- no Regexp.
  def dechunk(raw)
    out = ""
    pos = 0
    n = raw.bytesize
    while pos < n
      line_end = raw.index("\r\n", pos)
      unless line_end
        @error = "chunked: no length line"
        return nil
      end
      # The length may be followed by ";extension"; stop at the first
      # non-hex-digit either way.
      size = 0
      i = pos
      digits = 0
      while i < line_end
        v = hex_value(raw.getbyte(i))
        break if v < 0
        size = size * 16 + v
        digits += 1
        i += 1
      end
      if digits == 0
        @error = "chunked: bad length line"
        return nil
      end
      return out if size == 0
      body_at = line_end + 2
      if body_at + size > n
        @error = "chunked: short chunk (#{n - body_at} of #{size})"
        return nil
      end
      out << raw.byteslice(body_at, size)
      if out.bytesize > MAX_BODY
        @error = "response too large"
        return nil
      end
      pos = body_at + size + 2   # skip the CRLF after the data
    end
    @error = "chunked: ended without a zero chunk"
    nil
  end

  def hex_value(b)
    return b - 48 if b >= 48 && b <= 57    # 0-9
    return b - 87 if b >= 97 && b <= 102   # a-f
    return b - 55 if b >= 65 && b <= 70    # A-F
    -1
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
