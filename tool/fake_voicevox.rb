#!/usr/bin/env ruby
# A stand-in for VOICEVOX, so the tts service can be exercised without one.
#
# It answers the two calls the service makes and nothing else:
#
#   POST /audio_query?text=..&speaker=N   -> a small JSON object
#   POST /synthesis?speaker=N             -> a WAV (PCM 16-bit mono)
#
# The WAV it returns is a sine whose pitch is derived from the text, so one
# answer can be told from another by ear or by spectrum. Real VOICEVOX returns
# 24 kHz; this does too by default, which is inside what play_wav accepts and
# unlike the APU's 15720 Hz, so a wrong resample shows up as a wrong note.
#
#   ruby tool/fake_voicevox.rb [--port 50021] [--host 0.0.0.0]
#                             [--rate 24000] [--freq 440] [--seconds 1.0]
#                             [--delay-ms 0] [--truncate] [--quiet]
#
# --delay-ms holds the response back (to test the client's timeout) and
# --truncate promises more body than it sends and then hangs up (to test that
# a half-written file never reaches the cache).

require "socket"
require "optparse"

opts = { port: 50021, host: "0.0.0.0", rate: 24000, freq: 440.0,
         seconds: 1.0, delay_ms: 0, truncate: false, quiet: false }
OptionParser.new do |o|
  o.on("--port N", Integer) { |v| opts[:port] = v }
  o.on("--host H")          { |v| opts[:host] = v }
  o.on("--rate N", Integer) { |v| opts[:rate] = v }
  o.on("--freq F", Float)   { |v| opts[:freq] = v }
  o.on("--seconds F", Float){ |v| opts[:seconds] = v }
  o.on("--delay-ms N", Integer) { |v| opts[:delay_ms] = v }
  o.on("--truncate")        { opts[:truncate] = true }
  o.on("--quiet")           { opts[:quiet] = true }
end.parse!

def sine_wav(freq, rate, seconds, amplitude = 0.9)
  frames = (rate * seconds).to_i
  fade = (rate * 5 / 1000.0).to_i
  fade = frames / 2 if fade > frames / 2
  samples = Array.new(frames) do |i|
    gain = 1.0
    gain = i.to_f / fade if fade > 0 && i < fade
    gain = (frames - 1 - i).to_f / fade if fade > 0 && i >= frames - fade
    v = Math.sin(2.0 * Math::PI * freq * i / rate) * amplitude * gain
    (v * 32767).round.clamp(-32768, 32767)
  end
  pcm = samples.pack("s<*")
  fmt = ["fmt ", 16, 1, 1, rate, rate * 2, 2, 16].pack("a4Vv2V2v2")
  data = ["data", pcm.bytesize].pack("a4V") + pcm
  ["RIFF", 4 + fmt.bytesize + data.bytesize, "WAVE"].pack("a4Va4") + fmt + data
end

# Pitch from the text, so two different sentences are distinguishable.
def freq_for(text, base)
  return base if text.to_s.empty?
  base + (text.bytes.sum % 8) * 55.0
end

def unescape(str)
  str.to_s.gsub("+", " ").gsub(/%([0-9A-Fa-f]{2})/) { $1.to_i(16).chr }
end

server = TCPServer.new(opts[:host], opts[:port])
unless opts[:quiet]
  warn "fake_voicevox on #{opts[:host]}:#{opts[:port]} " \
       "(#{opts[:rate]} Hz, #{opts[:seconds]} s#{opts[:truncate] ? ', truncating' : ''})"
end

loop do
  conn = server.accept
  begin
    request_line = conn.gets
    next unless request_line
    _method, target, = request_line.split(" ")
    headers = {}
    while (line = conn.gets)
      break if line == "\r\n" || line == "\n"
      k, v = line.split(":", 2)
      headers[k.to_s.strip.downcase] = v.to_s.strip
    end
    body = ""
    if (len = headers["content-length"].to_i) > 0
      body = conn.read(len).to_s
    end

    path, query = target.to_s.split("?", 2)
    params = {}
    query.to_s.split("&").each do |kv|
      k, v = kv.split("=", 2)
      params[k] = unescape(v)
    end

    sleep(opts[:delay_ms] / 1000.0) if opts[:delay_ms] > 0

    case path
    when "/audio_query"
      text = params["text"].to_s
      # Only the field the service passes straight back matters; the shape is
      # what real VOICEVOX returns, trimmed to what a stand-in needs.
      json = %({"accent_phrases":[],"speedScale":1.0,"pitchScale":0.0,) +
             %("outputSamplingRate":#{opts[:rate]},"outputStereo":false,) +
             %("kana":"#{text.gsub('"', '')}"})
      warn "  audio_query text=#{text.inspect} speaker=#{params['speaker']}" unless opts[:quiet]
      conn.write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n" \
                 "Content-Length: #{json.bytesize}\r\nConnection: close\r\n\r\n")
      conn.write(json)
    when "/synthesis"
      kana = body[/"kana":"([^"]*)"/, 1].to_s
      wav = sine_wav(freq_for(kana, opts[:freq]), opts[:rate], opts[:seconds])
      warn "  synthesis kana=#{kana.inspect} -> #{wav.bytesize} bytes" unless opts[:quiet]
      conn.write("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\n" \
                 "Content-Length: #{wav.bytesize}\r\nConnection: close\r\n\r\n")
      conn.write(opts[:truncate] ? wav[0, wav.bytesize / 3] : wav)
    else
      warn "  404 #{path}" unless opts[:quiet]
      conn.write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
    end
  rescue => e
    warn "  error: #{e.class}: #{e.message}" unless opts[:quiet]
  ensure
    conn.close rescue nil
  end
end
