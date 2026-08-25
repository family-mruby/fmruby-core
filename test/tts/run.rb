# Host tests for the tts service's pure parts.
#
# Everything that does not touch the network or the filesystem runs here under
# host Ruby against the shipped file: the cache key, the URL encoder, the
# server-string split, and the HTTP response reader (fed from a fake socket).
# Those are the pieces where a mistake is silent -- a key that changes between
# runs empties the cache invisibly, and a response reader that mis-counts the
# body writes a truncated WAV.
#
#   ruby test/tts/run.rb

$failures = 0

def check(label)
  ok = yield
  if ok
    puts "ok   #{label}"
  else
    puts "FAIL #{label}"
    $failures += 1
  end
rescue => e
  puts "FAIL #{label} (#{e.class}: #{e.message})"
  $failures += 1
end

def eq(label, actual, expected)
  check("#{label} (#{actual.inspect})") { actual == expected }
end

# ---- stand-ins for what the firmware provides -----------------------------

module Machine
  def self.board_millis
    @t ||= 0
    @t += 1
    @t
  end

  def self.posix?
    true
  end
end

class SSLContext
  VERIFY_NONE = 0
  attr_accessor :verify_mode, :ca_file
end

class SSLSocket
  def self.open(host, port, ctx); raise "not used in these tests"; end
end

class TCPSocket
  @@timeout_ms = 10000
  def self.timeout_ms; @@timeout_ms; end
  def self.timeout_ms=(v); @@timeout_ms = v; end
end

# Hands out the bytes it was given, in the pieces it was given, then EOF --
# so a reader that assumes one read per response fails here.
class FakeSocket
  def initialize(chunks)
    @chunks = chunks.dup
  end

  def readpartial(_maxlen)
    raise EOFError, "eof" if @chunks.empty?
    @chunks.shift
  end
end

SERVICE = File.expand_path("../../flash/usr/share/services/tts.rb", __dir__)
HTTP_PART = File.expand_path("../../flash/usr/share/services/tts_http.rb", __dir__)

# The service requires its HTTP half by absolute device path; here the two
# files are simply loaded in order.
load HTTP_PART
def require(_path); true; end
load SERVICE

# ---- cache key -------------------------------------------------------------

svc = TtsService.new
svc.instance_variable_set(:@server, "http://10.0.0.1:50021")

check("the key is 16 hex characters") do
  k = svc.cache_key("こんにちは", 1)
  k.length == 16 && k =~ /\A[0-9a-f]{16}\z/
end

check("the same text and speaker give the same key") do
  svc.cache_key("あさです", 1) == svc.cache_key("あさです", 1)
end

check("a different speaker gives a different key") do
  svc.cache_key("あさです", 1) != svc.cache_key("あさです", 2)
end

check("a different text gives a different key") do
  svc.cache_key("あさです", 1) != svc.cache_key("よるです", 1)
end

check("a different server gives a different key") do
  other = TtsService.new
  other.instance_variable_set(:@server, "http://10.0.0.2:50021")
  svc.cache_key("あさです", 1) != other.cache_key("あさです", 1)
end

check("texts of the same length that differ still differ") do
  svc.cache_key("abcd", 1) != svc.cache_key("abce", 1)
end

check("the path lands in the cache directory") do
  svc.cache_path("x", 1).start_with?("/home/voice/cache/") &&
    svc.cache_path("x", 1).end_with?(".wav")
end

# ---- url encoding ----------------------------------------------------------

eq("ASCII passes through", svc.url_encode("hello"), "hello")
eq("unreserved characters pass through", svc.url_encode("a-b_c.d~e"), "a-b_c.d~e")
eq("a space is escaped", svc.url_encode("a b"), "a%20b")
eq("an ampersand is escaped", svc.url_encode("a&b=c"), "a%26b%3Dc")
check("UTF-8 goes out byte by byte") do
  # あ is E3 81 82
  svc.url_encode("あ") == "%E3%81%82"
end

# ---- server string ---------------------------------------------------------

eq("host and port", svc.split_server("http://192.168.10.5:50021"),
   ["192.168.10.5", 50021])
eq("no port means 80", svc.split_server("http://example.local"),
   ["example.local", 80])
eq("a trailing path is dropped", svc.split_server("http://h:8080/x/y"),
   ["h", 8080])
eq("no scheme still works", svc.split_server("h:1234"), ["h", 1234])
eq("nil means cache-only", svc.split_server(nil), [nil, nil])
eq("empty means cache-only", svc.split_server(""), [nil, nil])

# ---- response reading ------------------------------------------------------

http = TtsHttp.new(3000)
far = 1 << 40   # a deadline that never expires

check("a whole response in one piece") do
  body = "hello body"
  raw = "HTTP/1.1 200 OK\r\nContent-Length: #{body.bytesize}\r\n\r\n#{body}"
  http.read_response(FakeSocket.new([raw]), far) == [200, body]
end

check("headers and body split across reads") do
  body = "x" * 100
  raw = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n#{body}"
  pieces = [raw[0, 12], raw[12, 20], raw[32..-1]]
  http.read_response(FakeSocket.new(pieces), far) == [200, body]
end

check("a body arriving in many pieces is reassembled") do
  body = "abcdefghij" * 10
  head = "HTTP/1.1 200 OK\r\nContent-Length: #{body.bytesize}\r\n\r\n"
  pieces = [head] + body.chars.each_slice(7).map(&:join)
  http.read_response(FakeSocket.new(pieces), far) == [200, body]
end

check("the header name is matched case-insensitively") do
  raw = "HTTP/1.1 200 OK\r\ncontent-length: 3\r\n\r\nabc"
  http.read_response(FakeSocket.new([raw]), far) == [200, "abc"]
end

check("a short body is refused rather than returned") do
  raw = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nonly ten!!"
  http.read_response(FakeSocket.new([raw]), far).nil?
end

check("no Content-Length means read until close") do
  raw = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nbody here"
  http.read_response(FakeSocket.new([raw]), far) == [200, "body here"]
end

check("a non-200 status comes back as itself") do
  raw = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"
  http.read_response(FakeSocket.new([raw]), far) == [404, ""]
end

check("a connection that closes before the headers is refused") do
  http.read_response(FakeSocket.new(["HTTP/1.1 200 OK\r\n"]), far).nil?
end

check("a body larger than the ceiling is refused") do
  raw = "HTTP/1.1 200 OK\r\nContent-Length: #{TtsHttp::MAX_BODY + 1}\r\n\r\n"
  http.read_response(FakeSocket.new([raw]), far).nil?
end

check("an expired deadline stops the read") do
  body = "x" * 100
  head = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n"
  # board_millis advances on every call, so a deadline of 0 is already past.
  http.read_response(FakeSocket.new([head, body]), 0).nil?
end

check("content_length ignores a header that is not a number") do
  http.content_length("HTTP/1.1 200 OK\r\nContent-Length: banana").nil?
end

eq("content_length reads a tab-separated value",
   http.content_length("HTTP/1.1 200 OK\r\nContent-Length:\t42\r\n"), 42)
eq("content_length stops at the line end",
   http.content_length("HTTP/1.1 200 OK\r\nContent-Length: 42\r\nX: 9"), 42)

# The status line is picked apart by hand because picoruby has no Regexp; the
# host has one, so only a test like this notices if a regexp creeps back in.
eq("a status line reads as its number", http.status_of("HTTP/1.1 204 No Content"), 204)
eq("HTTP/1.0 is read too", http.status_of("HTTP/1.0 200 OK"), 200)
eq("a line that is not a status line reads as 0", http.status_of("GARBAGE"), 0)
eq("a non-numeric code reads as 0", http.status_of("HTTP/1.1 2x0 Huh"), 0)

check("the service files use no regexp literals (picoruby has no Regexp)") do
  # A regexp literal in a shipped file compiles fine here and raises NameError
  # on the device, which is how this was found the first time.
  [SERVICE, HTTP_PART].all? do |f|
    body = File.read(f)
    !body.include?("[/") && !body.match?(/=~/)
  end
end

check("neither service file is big enough to kill the on-device compile") do
  # One file with all of this in it took the service host down during require,
  # with no exception to show for it, and the limit is the amount of code
  # rather than the size of the file (stripping the comments did not help).
  # 10 KB of code each is comfortably inside what loads.
  [SERVICE, HTTP_PART].all? do |f|
    code = File.readlines(f).reject { |l| l.strip.empty? || l.strip.start_with?("#") }.join
    code.bytesize < 10_240
  end
end

# ---- chunked bodies --------------------------------------------------------

check("a chunked body is unwrapped") do
  raw = "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n"
  http.dechunk(raw) == "Wikipedia"
end

check("a chunk length may carry an extension") do
  raw = "4;foo=bar\r\nWiki\r\n0\r\n\r\n"
  http.dechunk(raw) == "Wiki"
end

check("upper-case hex lengths are read") do
  raw = "A\r\n0123456789\r\n0\r\n\r\n"
  http.dechunk(raw) == "0123456789"
end

check("a chunk that promises more than it sends is refused") do
  http.dechunk("10\r\nshort\r\n0\r\n\r\n").nil?
end

check("a stream with no zero chunk is refused") do
  http.dechunk("4\r\nWiki\r\n").nil?
end

check("binary chunk data survives") do
  payload = (0..255).map(&:chr).join
  raw = "#{payload.bytesize.to_s(16)}\r\n#{payload}\r\n0\r\n\r\n"
  http.dechunk(raw) == payload
end

check("a chunked response reads end to end") do
  head = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
  raw = head + "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n"
  http.read_response(FakeSocket.new([raw]), far) == [200, "Wikipedia"]
end

check("a chunked response split across reads reads end to end") do
  head = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
  raw = head + "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n"
  pieces = raw.chars.each_slice(9).map(&:join)
  http.read_response(FakeSocket.new(pieces), far) == [200, "Wikipedia"]
end

check("Content-Length wins over a chunked header if both are present") do
  raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 3\r\n\r\nabc"
  http.read_response(FakeSocket.new([raw]), far) == [200, "abc"]
end

check("chunked? is matched case-insensitively on its own line") do
  http.chunked?("HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\nX: y") &&
    !http.chunked?("HTTP/1.1 200 OK\r\nX-Note: chunked is not used here\r\nY: z")
end

# ---- the cloud request -----------------------------------------------------

cloud = TtsService.new
cloud.instance_variable_set(:@server, nil)
cloud.instance_variable_set(:@cloud_model, "gpt-4o-mini-tts")
cloud.instance_variable_set(:@cloud_voice, "alloy")

eq("plain text needs no escaping", cloud.json_escape("hello"), "hello")
eq("a quote is escaped", cloud.json_escape('say "hi"'), 'say \\"hi\\"')
eq("a backslash is escaped", cloud.json_escape("a\\b"), "a\\\\b")
eq("a newline becomes \\n", cloud.json_escape("a\nb"), "a\\nb")
check("UTF-8 passes through unescaped (JSON allows it)") do
  cloud.json_escape("こんにちは") == "こんにちは"
end
check("other control characters become spaces") do
  cloud.json_escape("a\u0007b") == "a b"
end

check("the cloud cache path is keyed by model and voice") do
  a = cloud.cloud_cache_path("おはよう")
  cloud.instance_variable_set(:@cloud_voice, "nova")
  b = cloud.cloud_cache_path("おはよう")
  a != b && a.start_with?("/home/voice/cache/") && a.end_with?(".wav")
end

check("a cloud key differs from a VOICEVOX key for the same text") do
  vv = TtsService.new
  vv.instance_variable_set(:@server, "http://10.0.0.1:50021")
  cloud.cloud_cache_path("おはよう") != vv.cache_path("おはよう", 1)
end

check("the VOICEVOX key did not change when the cloud one was added") do
  # Guards the existing caches on real machines: this is the exact seed the
  # first version hashed.
  vv = TtsService.new
  vv.instance_variable_set(:@server, "http://10.0.0.1:50021")
  seed = "http://10.0.0.1:50021|1|あさです"
  h = 5381
  seed.bytes.each { |b| h = ((h * 33) + b) & 0xFFFFFFFF }
  vv.cache_key("あさです", 1) == ("%08x%08x" % [h, seed.bytesize])
end

if $failures > 0
  puts "tts: #{$failures} failure(s)"
  exit 1
end
puts "tts: all checks passed"
