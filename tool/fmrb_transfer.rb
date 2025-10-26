#!/usr/bin/env ruby
# sercli.rb - Simple CLI for ESP32 (custom UART protocol)
# Dependencies: gem install serialport
require 'json'
require 'optparse'
require 'serialport'
require 'zlib'
require 'readline'

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

# ===== Serial Transfer Client (Custom Simple Protocol) =====
# Frame: COBS( [cmd(1B)] [len(2B BE)] [payload bytes] [CRC32(4B)] ) + 0x00
# payload: JSON (UTF-8) or JSON + binary (for GET/PUT)
class SerialClient
  DELIM = "\x00"

  def initialize(port:, baud:115200, rtscts:true, timeout_s:5.0)
    # Resolve symlink if needed
    real_port = File.symlink?(port) ? File.readlink(port) : port

    # For PTY devices, use File.open instead of SerialPort
    if real_port.include?("pts") || real_port.include?("tty")
      @sp = File.open(real_port, "r+b")
      @sp.sync = true
      @is_pty = true
      @timeout_ms = (timeout_s * 1000).to_i
    else
      @sp = SerialPort.new(real_port, baud, 8, 1, SerialPort::NONE)
      @sp.binmode if @sp.respond_to?(:binmode)

      # Set flow control if supported and requested
      if rtscts && @sp.respond_to?(:flow_control=)
        begin
          @sp.flow_control = SerialPort::HARDWARE
        rescue NameError
          @sp.flow_control = (defined?(SerialPort::HARD) ? SerialPort::HARD : 1)
        end
      end
      @sp.read_timeout = (timeout_s * 1000).to_i if @sp.respond_to?(:read_timeout=)
      @is_pty = false
      @timeout_ms = (timeout_s * 1000).to_i
    end

    @rx = String.new(encoding: "ASCII-8BIT")
  end

  def close; @sp.close rescue nil; end

  # --- High-level commands ---
  def r_cd(path)  = cmd_simple(0x11, path: path)         # remote cd
  def r_ls(path=".") = cmd_simple(0x12, path: path)      # remote ls -> entries
  def r_rm(path)  = cmd_simple(0x13, path: path)         # remote rm (file/dir depends on implementation)

  def h_cd(path)  = Dir.chdir(path)
  def h_ls(path="."); Dir.children(path).sort.each { |e| puts e } end

  # transfer up: PC->ESP32, down: ESP32->PC
  def transfer(direction, local:, remote:, chunk: 4096)
    case direction
    when "up"   then put(local, remote, chunk: chunk)
    when "down" then get(remote, local, chunk: chunk)
    else raise "transfer: direction must be 'up' or 'down'"
    end
  end

  # --- Low-level (GET/PUT) ---
  def get(remote_path, local_path, chunk: 4096)
    File.open(local_path, "wb") do |f|
      off = 0
      loop do
        meta, data = request_bin(0x21, {path: remote_path, off: off}.to_json)
        raise "GET failed: #{meta}" unless meta["ok"]
        if data && !data.empty?
          f.write(data)
          off += data.bytesize
        end
        break if meta["eof"]
      end
    end
    true
  end

  def put(local_path, remote_path, chunk: 4096)
    off = 0
    File.open(local_path, "rb") do |f|
      loop do
        buf = f.read(chunk) || ""
        meta = request(0x22, {path: remote_path, off: off}.to_json, buf)
        raise "PUT failed: #{meta}" unless meta["ok"]
        off += buf.bytesize
        break if buf.empty?
      end
    end
    true
  end

  private

  def cmd_simple(code, obj)
    res = request(code, obj.to_json)
    raise "Remote error: #{res}" unless res["ok"]
    res["entries"] || true
  end

  def request(code, json, bin=nil)
    pkt = build_packet(code, json, bin)
    @sp.write(pkt + DELIM)
    meta, _ = read_response
    meta
  end

  def request_bin(code, json, bin=nil)
    pkt = build_packet(code, json, bin)
    @sp.write(pkt + DELIM)
    read_response # => [meta, data]
  end

  def build_packet(code, json, bin)
    body = [code].pack("C")
    json_data = json.force_encoding("ASCII-8BIT")
    # len field contains only JSON length, binary data follows separately
    body << [json_data.bytesize].pack("n") << json_data
    body << bin if bin  # Append binary data after JSON
    crc = [Zlib.crc32(body)].pack("N")
    raw = body + crc
    COBS.encode(raw)
  end

  def read_response
    timeout_time = Time.now + (@timeout_ms / 1000.0)
    loop do
      ready = IO.select([@sp], nil, nil, 1.0)

      if ready.nil?
        raise "Timeout waiting frame" if Time.now > timeout_time
        next
      end

      begin
        chunk = @sp.read_nonblock(2048)
      rescue IO::WaitReadable
        next
      rescue EOFError
        raise "Connection closed"
      end

      chunk = chunk.force_encoding("ASCII-8BIT")
      @rx << chunk
      if (i = @rx.index(DELIM))
        frame = @rx.slice!(0, i)
        @rx.slice!(0, 1)
        raw = COBS.decode(frame)
        raise "Short frame" if raw.bytesize < 5
        body = raw[0...-4]
        crc  = raw[-4..-1]
        raise "CRC error" unless [Zlib.crc32(body)].pack("N") == crc
        code = body.getbyte(0)
        len  = body.byteslice(1,2).unpack1("n")
        pay  = body.byteslice(3, len)
        # Response format: len field contains JSON length only, binary data follows after JSON in body
        meta = JSON.parse(pay.force_encoding("UTF-8"), symbolize_names: false, create_additions: false) rescue nil
        data = nil
        if meta && meta["bin"].is_a?(Integer)
          # Extract binary data from body (not pay), starting after JSON
          data = body.byteslice(3 + len, meta["bin"])
        end
        return [meta || {"ok"=>false,"err"=>"bad_json"}, data]
      end
    end
  end
end

# ===== Interactive Shell =====
class InteractiveShell
  def initialize(cli:)
    @cli = cli
    @remote_pwd = "/"
    @local_pwd = Dir.pwd
  end

  def run
    puts "=== FMRuby Transfer Shell ==="
    puts "Type 'help' for commands, 'exit' or 'quit' to exit"
    puts ""

    loop do
      begin
        prompt = "fmrb [R:#{@remote_pwd} L:#{@local_pwd}]> "
        line = Readline.readline(prompt, true)

        break if line.nil? # Ctrl-D
        line = line.strip
        next if line.empty?

        # Remove empty lines from history
        Readline::HISTORY.pop if line.empty?

        args = parse_line(line)
        cmd = args.shift

        break if cmd == "exit" || cmd == "quit"

        execute_command(cmd, args)
      rescue Interrupt
        puts "\nUse 'exit' or 'quit' to exit"
      rescue => e
        puts "Error: #{e.message}"
      end
    end

    puts "Goodbye!"
  end

  private

  def parse_line(line)
    # Simple shell-like parsing (quote support)
    args = []
    current = ""
    in_quote = false

    line.each_char do |c|
      case c
      when '"', "'"
        in_quote = !in_quote
      when ' '
        if in_quote
          current << c
        else
          args << current unless current.empty?
          current = ""
        end
      else
        current << c
      end
    end
    args << current unless current.empty?
    args
  end

  def execute_command(cmd, args)
    case cmd
    when "help"
      show_help
    when "lcd"
      local_cd(args[0] || Dir.home)
    when "lls"
      local_ls(args[0] || ".")
    when "lpwd"
      puts @local_pwd
    when "cd"
      remote_cd(args[0] || "/")
    when "ls"
      remote_ls(args[0] || ".")
    when "pwd"
      puts @remote_pwd
    when "rm"
      return puts "Usage: rm <path>" if args.empty?
      remote_rm(args[0])
    when "get"
      return puts "Usage: get <remote_path> [local_path]" if args.empty?
      remote_path = args[0]
      local_path = args[1] || File.basename(remote_path)
      download(remote_path, local_path)
    when "put"
      return puts "Usage: put <local_path> [remote_path]" if args.empty?
      local_path = args[0]
      remote_path = args[1] || File.basename(local_path)
      upload(local_path, remote_path)
    else
      puts "Unknown command: #{cmd}"
      puts "Type 'help' for available commands"
    end
  end

  def show_help
    puts <<~HELP
      Available commands:

      Remote (ESP32) operations:
        cd <path>              Change remote directory
        ls [path]              List remote directory
        pwd                    Print remote working directory
        rm <path>              Remove remote file/directory
        get <remote> [local]   Download file from ESP32
        put <local> [remote]   Upload file to ESP32

      Local (PC) operations:
        lcd <path>             Change local directory
        lls [path]             List local directory
        lpwd                   Print local working directory

      Other:
        help                   Show this help
        exit, quit             Exit shell
    HELP
  end

  def local_cd(path)
    Dir.chdir(path)
    @local_pwd = Dir.pwd
    puts @local_pwd
  end

  def local_ls(path)
    entries = Dir.children(path).sort
    entries.each { |e| puts e }
  end

  def remote_cd(path)
    # Resolve relative path
    new_path = resolve_remote_path(path)
    @cli.r_cd(new_path)
    @remote_pwd = new_path
    puts @remote_pwd
  end

  def remote_ls(path)
    full_path = resolve_remote_path(path)
    entries = @cli.r_ls(full_path)
    entries.each do |e|
      mark = (e["t"] == "d") ? "/" : ""
      size = e["s"] || 0
      puts "#{e["n"]}#{mark}\t#{size}"
    end
  end

  def remote_rm(path)
    full_path = resolve_remote_path(path)
    @cli.r_rm(full_path)
    puts "Removed: #{full_path}"
  end

  def download(remote_path, local_path)
    full_remote = resolve_remote_path(remote_path)
    full_local = File.expand_path(local_path, @local_pwd)
    puts "Downloading #{full_remote} -> #{full_local}"
    @cli.transfer("down", local: full_local, remote: full_remote)
    puts "Download complete"
  end

  def upload(local_path, remote_path)
    full_local = File.expand_path(local_path, @local_pwd)
    full_remote = resolve_remote_path(remote_path)
    puts "Uploading #{full_local} -> #{full_remote}"
    @cli.transfer("up", local: full_local, remote: full_remote)
    puts "Upload complete"
  end

  def resolve_remote_path(path)
    return path if path.start_with?("/")

    # For relative paths, resolve from current remote directory
    if @remote_pwd == "/"
      "/#{path}"
    else
      "#{@remote_pwd}/#{path}"
    end
  end
end

# ===== CLI =====
def usage!
  puts <<~USAGE
    Usage:
      # Interactive shell mode
      sercli.rb --port COM3 [--baud 115200] shell

      # One-shot commands
      # Remote (ESP32)
      sercli.rb --port COM3 remote cd <path>
      sercli.rb --port COM3 remote ls [path]
      sercli.rb --port COM3 remote rm <path>

      # Host (PC)
      sercli.rb host cd <path>
      sercli.rb host ls [path]

      # Transfer
      sercli.rb --port COM3 transfer up   <local>  <remote>
      sercli.rb --port COM3 transfer down <remote> <local>

    Options:
      --rtscts[=true|false]   default: true
      --timeout SEC           default: 5
  USAGE
  exit 1
end

opts = { baud: 115200, rtscts: true, timeout: 5.0, port: nil }
parser = OptionParser.new do |o|
  o.on("--port PORT"){|v| opts[:port]=v }
  o.on("--baud N", Integer){|v| opts[:baud]=v }
  o.on("--rtscts[=FLAG]"){|v| opts[:rtscts] = v.nil? ? true : (v =~ /^(?:1|t|true|yes)$/i) }
  o.on("--timeout SEC", Float){|v| opts[:timeout]=v }
end
begin
  parser.order!
rescue
  usage!
end

cmd1 = ARGV.shift or usage!

case cmd1
when "shell"
  raise "--port required" unless opts[:port]
  cli = SerialClient.new(port: opts[:port], baud: opts[:baud], rtscts: opts[:rtscts], timeout_s: opts[:timeout])
  begin
    shell = InteractiveShell.new(cli: cli)
    shell.run
  ensure
    cli.close
  end

when "remote"
  sub = ARGV.shift or usage!
  raise "--port required" unless opts[:port]
  cli = SerialClient.new(port: opts[:port], baud: opts[:baud], rtscts: opts[:rtscts], timeout_s: opts[:timeout])
  begin
    case sub
    when "cd"
      path = ARGV.shift or usage!
      cli.r_cd(path)
    when "ls"
      path = ARGV.shift || "."
      entries = cli.r_ls(path)
      # Format display
      entries.each do |e|
        # e = {"n","s","t"} where t: "f"|"d"
        mark = (e["t"]=="d") ? "/" : ""
        puts "#{e["n"]}#{mark}\t#{e["s"]}"
      end
    when "rm"
      path = ARGV.shift or usage!
      cli.r_rm(path)
    else
      usage!
    end
  ensure
    cli.close
  end

when "host"
  sub = ARGV.shift or usage!
  case sub
  when "cd"
    path = ARGV.shift or usage!
    Dir.chdir(path)
  when "ls"
    path = ARGV.shift || "."
    Dir.children(path).sort.each { |e| puts e }
  else
    usage!
  end

when "transfer"
  dir = ARGV.shift or usage!
  raise "--port required" unless opts[:port]
  cli = SerialClient.new(port: opts[:port], baud: opts[:baud], rtscts: opts[:rtscts], timeout_s: opts[:timeout])
  begin
    case dir
    when "up"
      local  = ARGV.shift or usage!
      remote = ARGV.shift or usage!
      cli.transfer("up", local: local, remote: remote)
    when "down"
      remote = ARGV.shift or usage!
      local  = ARGV.shift or usage!
      cli.transfer("down", local: local, remote: remote)
    else
      usage!
    end
  ensure
    cli.close
  end

else
  usage!
end
