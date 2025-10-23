#!/usr/bin/env ruby
# fmrb_test_server.rb - Test server for fmrb_transfer.rb using socat
# Dependencies: gem install serialport

require 'json'
require 'serialport'
require 'zlib'
require 'fileutils'

# ===== COBS (Consistent Overhead Byte Stuffing) =====
module COBS
  def self.encode(data)
    data = data.force_encoding("ASCII-8BIT")
    out = String.new(encoding: "ASCII-8BIT")
    code_index = 0
    out << "\x00" # placeholder
    code = 1
    data.each_byte do |b|
      if b == 0
        out.setbyte(code_index, code)
        code_index = out.bytesize
        out << "\x00"
        code = 1
      else
        out << [b].pack("C")
        code += 1
        if code == 0xFF
          out.setbyte(code_index, code)
          code_index = out.bytesize
          out << "\x00"
          code = 1
        end
      end
    end
    out.setbyte(code_index, code)
    out
  end

  def self.decode(data)
    data = data.force_encoding("ASCII-8BIT")
    out = String.new(encoding: "ASCII-8BIT")
    i = 0
    while i < data.bytesize
      code = data.getbyte(i) or raise "COBS decode error"
      raise "COBS decode error" if code == 0
      i += 1
      (code - 1).times do
        raise "COBS overrun" if i >= data.bytesize
        out << [data.getbyte(i)].pack("C")
        i += 1
      end
      out << "\x00" if code < 0xFF && i < data.bytesize
    end
    out
  end
end

# ===== Test Server (simulates ESP32 file system) =====
class TestServer
  DELIM = "\x00"

  def initialize(port:, root_dir: "/tmp/fmrb_test")
    # Resolve symlink if needed
    real_port = File.symlink?(port) ? File.readlink(port) : port

    # For PTY devices, use File.open instead of SerialPort
    if real_port.include?("pts") || real_port.include?("tty")
      @sp = File.open(real_port, "r+b")
      @sp.sync = true
    else
      @sp = SerialPort.new(real_port, 115200, 8, 1, SerialPort::NONE)
      @sp.binmode if @sp.respond_to?(:binmode)
      @sp.read_timeout = 5000
    end

    @rx = String.new(encoding: "ASCII-8BIT")
    @root_dir = File.expand_path(root_dir)
    @current_dir = "/"

    # Create test directory structure
    FileUtils.mkdir_p(@root_dir)
    puts "Test server started"
    puts "Root directory: #{@root_dir}"
    puts "Listening on port: #{real_port}"
    puts "Waiting for commands..."
  end

  def run
    loop do
      begin
        frame = read_frame
        next unless frame

        process_frame(frame)
      rescue => e
        puts "Error: #{e.message}"
        puts e.backtrace.first(5)
      end
    end
  end

  private

  def read_frame
    loop do
      # Use IO.select for PTY devices
      ready = IO.select([@sp], nil, nil, 1.0)
      next if ready.nil?

      begin
        chunk = @sp.read_nonblock(2048)
      rescue IO::WaitReadable
        next
      rescue EOFError
        return nil
      end

      chunk = chunk.force_encoding("ASCII-8BIT")
      @rx << chunk
      if (i = @rx.index(DELIM))
        frame = @rx.slice!(0, i)
        @rx.slice!(0, 1)
        return frame
      end
    end
  end

  def process_frame(frame)
    raw = COBS.decode(frame)
    raise "Short frame" if raw.bytesize < 5

    body = raw[0...-4]
    crc  = raw[-4..-1]
    raise "CRC error" unless [Zlib.crc32(body)].pack("N") == crc

    cmd = body.getbyte(0)
    len = body.byteslice(1, 2).unpack1("n")
    payload = body.byteslice(3, len)

    # Parse JSON from payload
    json_str = payload.force_encoding("UTF-8")
    params = JSON.parse(json_str, symbolize_names: true) rescue {}

    # Extract binary data if present (for PUT command)
    binary_data = nil
    if params.is_a?(Hash)
      json_len = params.to_json.bytesize
      binary_data = payload.byteslice(json_len..-1) if json_len < payload.bytesize
    end

    puts "CMD: 0x#{cmd.to_s(16).rjust(2, '0')} #{params}"

    response = case cmd
    when 0x11 then cmd_cd(params)
    when 0x12 then cmd_ls(params)
    when 0x13 then cmd_rm(params)
    when 0x21 then cmd_get(params)
    when 0x22 then cmd_put(params, binary_data)
    else
      {ok: false, err: "unknown_command"}
    end

    send_response(response)
  end

  def cmd_cd(params)
    path = params[:path] || "/"
    full_path = resolve_path(path)

    unless File.directory?(full_path)
      return {ok: false, err: "not_found"}
    end

    @current_dir = path.start_with?("/") ? path : File.join(@current_dir, path)
    @current_dir = "/" + @current_dir.split("/").reject(&:empty?).join("/")

    puts "  -> CD to #{@current_dir}"
    {ok: true}
  end

  def cmd_ls(params)
    path = params[:path] || "."
    full_path = resolve_path(path)

    unless File.directory?(full_path)
      return {ok: false, err: "not_found"}
    end

    entries = Dir.children(full_path).map do |name|
      item_path = File.join(full_path, name)
      {
        n: name,
        s: File.size(item_path),
        t: File.directory?(item_path) ? "d" : "f"
      }
    end

    {ok: true, entries: entries}
  end

  def cmd_rm(params)
    path = params[:path]
    return {ok: false, err: "no_path"} unless path

    full_path = resolve_path(path)

    unless File.exist?(full_path)
      return {ok: false, err: "not_found"}
    end

    if File.directory?(full_path)
      FileUtils.rm_rf(full_path)
    else
      File.delete(full_path)
    end

    puts "  -> RM #{path}"
    {ok: true}
  end

  def cmd_get(params)
    path = params[:path]
    offset = params[:off] || 0

    return {ok: false, err: "no_path"} unless path

    full_path = resolve_path(path)

    unless File.file?(full_path)
      return {ok: false, err: "not_found"}
    end

    chunk_size = 4096
    data = nil
    eof = false

    File.open(full_path, "rb") do |f|
      f.seek(offset)
      data = f.read(chunk_size) || ""
      eof = f.eof?
    end

    puts "  -> GET #{path} offset=#{offset} size=#{data.bytesize} eof=#{eof}"

    # Return response with binary data
    {ok: true, eof: eof, bin: data.bytesize, _binary: data}
  end

  def cmd_put(params, binary_data)
    path = params[:path]
    offset = params[:off] || 0

    return {ok: false, err: "no_path"} unless path

    full_path = resolve_path(path)
    dir = File.dirname(full_path)
    FileUtils.mkdir_p(dir) unless File.directory?(dir)

    mode = offset == 0 ? "wb" : "ab"
    File.open(full_path, mode) do |f|
      f.write(binary_data) if binary_data
    end

    puts "  -> PUT #{path} offset=#{offset} size=#{binary_data&.bytesize || 0}"
    {ok: true}
  end

  def resolve_path(path)
    if path.start_with?("/")
      File.join(@root_dir, path)
    elsif path == "."
      File.join(@root_dir, @current_dir)
    else
      File.join(@root_dir, @current_dir, path)
    end
  end

  def send_response(response)
    # Extract binary data if present
    binary = response.delete(:_binary)

    # Build JSON response
    json = response.to_json
    payload = json.force_encoding("ASCII-8BIT")
    payload = +payload # make mutable
    payload << binary if binary

    # Build packet
    body = [0x00].pack("C") # response code
    body << [payload.bytesize].pack("n") << payload
    crc = [Zlib.crc32(body)].pack("N")

    raw = body + crc
    encoded = COBS.encode(raw)
    packet = encoded + DELIM

    @sp.write(packet)
  end
end

# ===== Main =====
if ARGV.size < 1
  puts "Usage: #{$0} <serial_port> [root_dir]"
  puts "Example: #{$0} /dev/pts/5 /tmp/fmrb_test"
  exit 1
end

port = ARGV[0]
root_dir = ARGV[1] || "/tmp/fmrb_test"

begin
  server = TestServer.new(port: port, root_dir: root_dir)
  server.run
rescue Interrupt
  puts "\nServer stopped"
rescue => e
  puts "Fatal error: #{e.message}"
  puts e.backtrace
  exit 1
end
