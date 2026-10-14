# Family mruby's default profile of an Asterism node (doc/ruby_asterism,
# V2.5, G1b): kind family_mruby, host from the board, and the machine's own
# name from /home/asterism_profile.toml. Runs when a VM starts, before the
# application, so an application's own Asterism.profile(...) changes it
# further. The icon is the gem's default file,
# /usr/share/asterism/icon.png (flash/usr/share/asterism/; the browser
# bundle stages its own icon there, see rakelib/wasm.rake); it is read when
# the profile is first needed (connect), not here.
#
# The name is the player's, the same in every application of the machine:
# one file in /home (the browser build keeps /home in the browser's
# storage). FmrbAsterism.save_name writes it and sets it in this VM's
# profile at once (a connected application puts the change on the
# network); applications started later read it here.
module FmrbAsterism
  # FmrbConst::BOARD -> the profile's host. The sim and the browser build
  # say what they are; NARYAv4 is the P4-Nano board.
  HOSTS = { "linux" => "sim", "wasm" => "browser", "naryav4" => "p4nano" }
  PROFILE_FILE = "/home/asterism_profile.toml"

  def self.host
    board = FmrbConst::BOARD
    HOSTS[board] || board
  end

  # The saved name ("" when there is none or the file cannot be read).
  def self.saved_name
    return "" unless File.exist?(PROFILE_FILE)
    text = File.open(PROFILE_FILE, "r") { |f| f.read }
    lines = text.to_s.split("\n")
    i = 0
    while i < lines.size
      v = toml_string(lines[i], "name")
      return v if v
      i += 1
    end
    ""
  rescue
    ""
  end

  # Saves the name (nil or "" removes it) and sets it in this VM's profile.
  # Returns the profile. Raises ArgumentError for a name the profile does
  # not take (longer than Asterism::Profile::NAME_MAX bytes).
  def self.save_name(name)
    name = name.to_s.strip
    Asterism.profile(name: name.empty? ? nil : name)
    File.open(PROFILE_FILE, "w") do |f|
      f.write("# This machine's Asterism profile (written by the applications)\n")
      f.write("name = \"#{toml_escape(name)}\"\n")
    end
    Asterism.profile
  end

  # key = "value" on one line -> value (only \" and \\ are unescaped), or nil.
  def self.toml_string(line, key)
    s = line.strip
    return nil unless s.start_with?(key)
    rest = s[key.length, s.length].to_s.strip
    return nil unless rest.start_with?("=")
    rest = rest[1, rest.length].to_s.strip
    return nil unless rest.start_with?("\"")
    out = "".dup
    i = 1
    n = rest.bytesize
    while i < n
      c = rest.byteslice(i, 1)
      if c == "\\" && i + 1 < n
        out << rest.byteslice(i + 1, 1)
        i += 2
        next
      end
      return out if c == "\""
      out << c
      i += 1
    end
    nil
  end

  # The machine's Asterism credentials for a tls/ locator (connecting
  # straight to a cloud router with mutual TLS, doc/ruby_asterism G2a): the
  # .tar the console hands out, put in this directory as it is. One file;
  # with more, the first by name is used. devctl's /fs/get does not read
  # this directory (it holds the private key).
  CREDENTIALS_DIR = "/home/asterism"

  # Whether locator ("tls/host:port", "tcp/...", "peer ...") asks for TLS.
  def self.tls?(locator)
    locator.to_s.start_with?("tls/")
  end

  # The path of the credentials .tar, or nil when there is none.
  def self.credentials_path
    names = Dir.children(CREDENTIALS_DIR).select { |n| n.end_with?(".tar") }.sort
    return nil if names.empty?
    if names.size > 1
      puts "asterism: #{names.size} credentials in #{CREDENTIALS_DIR}, using #{names[0]}"
    end
    "#{CREDENTIALS_DIR}/#{names[0]}"
  rescue
    nil
  end

  # The loaded credentials (Asterism::Credentials) for a tls/ locator, nil
  # for any other. Raises Asterism::Error when TLS is asked for and there
  # is no credentials file, or it cannot be read; the node ID of the
  # connection is then the certificate's name (creds.name), which the
  # router's access control knows the machine by.
  def self.credentials_for(locator)
    return nil unless tls?(locator)
    path = credentials_path
    raise Asterism::Error, "#{locator} needs a credentials .tar in #{CREDENTIALS_DIR}" if path.nil?
    Asterism::Credentials.load(path)
  end

  def self.toml_escape(s)
    out = "".dup
    i = 0
    n = s.bytesize
    while i < n
      c = s.byteslice(i, 1)
      if c == "\\" || c == "\""
        out << "\\" << c
      elsif c.getbyte(0) >= 32
        out << c
      end
      i += 1
    end
    out
  end
end

Asterism.profile(kind: :family_mruby, host: FmrbAsterism.host)
begin
  n = FmrbAsterism.saved_name
  Asterism.profile(name: n) unless n.empty?
rescue ArgumentError
  # a name the profile does not take (edited by hand): left out
end
