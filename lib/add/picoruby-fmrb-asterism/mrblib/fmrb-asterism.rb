# Family mruby's defaults for Asterism (doc/ruby_asterism): the default
# profile of a node (V2.5, G1b) and the one place that says how this machine
# connects (CF1).
#
# Profile: kind family_mruby, host from the board, and the machine's own
# name. Runs when a VM starts, before the application, so an application's
# own Asterism.profile(...) changes it further. The icon is the gem's
# default file, /usr/share/asterism/icon.png (flash/usr/share/asterism/;
# the browser bundle stages its own icon there, see rakelib/wasm.rake); it
# is read when the profile is first needed (connect), not here.
#
# Settings (CF1): everything about the connection lives in /home/asterism/
# (the browser build keeps /home in the browser's storage):
#
#   config.toml   router = "tls/host:port"   the router's locator
#                 node   = "..."             the node ID (optional)
#                 name   = "..."             the machine's name, the player's
#                                            (the profile's name)
#   <name>.tar    the credentials the console hands out (G2a), as it is
#
# FmrbAsterism.settings resolves them for an application: the router from
# config.toml, else the machine's zenoh_router setting (the browser page's),
# else the credentials' asterism.json, else tcp/zenohd:7447; the node ID
# from config.toml, else the mDNS name, else the asterism_node setting (the
# browser's wasm-XXXXXX), else the board name -- and with credentials the
# certificate's name, which the router's access control knows the machine
# by. Only the keys written in config.toml are taken from it.
#
# The files before CF1 (/home/zenoh_echo.txt: the router, the first line;
# /home/asterism_node.txt: the node ID; /home/asterism_profile.toml: the
# name) are read when config.toml does not exist and written into it once.
# They are left where they are: an app-only flash updates the firmware (and
# this file) but not the applications in storage, and the older applications
# still read them. Once config.toml exists they are not read; an
# application that resolves its settings names them in one log line.
#
# The name is the same in every application of the machine.
# FmrbAsterism.save_name writes it and sets it in this VM's profile at once
# (a connected application puts the change on the network); applications
# started later read it here.
module FmrbAsterism
  # FmrbConst::BOARD -> the profile's host. The sim and the browser build
  # say what they are; NARYAv4 is the P4-Nano board.
  HOSTS = { "linux" => "sim", "wasm" => "browser", "naryav4" => "p4nano" }
  DIR = "/home/asterism"
  CONFIG_FILE = "/home/asterism/config.toml"
  CONFIG_KEYS = ["router", "node", "name"]
  # The files before CF1: [key, path, how it is written].
  OLD_FILES = [
    ["router", "/home/zenoh_echo.txt", :line],
    ["node", "/home/asterism_node.txt", :line],
    ["name", "/home/asterism_profile.toml", :toml]
  ]
  SYSTEM_CONF = "/etc/system_conf.toml"
  DEFAULT_ROUTER = "tcp/zenohd:7447"

  def self.host
    board = FmrbConst::BOARD
    HOSTS[board] || board
  end

  # What config.toml says ({"router" => ..., "node" => ..., "name" => ...},
  # only the keys written in it; {} without the file). Without the file, the
  # files before CF1 are moved into it first.
  def self.config
    return read_config if File.exist?(CONFIG_FILE)
    migrate
  end

  # The saved name ("" when there is none or the file cannot be read).
  def self.saved_name
    config["name"].to_s
  rescue
    ""
  end

  # Saves the name (nil or "" removes it) and sets it in this VM's profile.
  # Returns the profile. Raises ArgumentError for a name the profile does
  # not take (longer than Asterism::Profile::NAME_MAX bytes).
  def self.save_name(name)
    name = name.to_s.strip
    Asterism.profile(name: name.empty? ? nil : name)
    c = config
    if name.empty?
      c.delete("name")
    else
      c["name"] = name
    end
    write_config(c)
    Asterism.profile
  end

  # config.toml -> Hash: the top-level string values of CONFIG_KEYS, read
  # through FmrbToml ({} without the file, or when it is not TOML -- that one
  # is logged).
  def self.read_config
    strings_of(FmrbToml.load(CONFIG_FILE), CONFIG_KEYS)
  end

  # The keys of keys whose values are Strings in conf (a Hash or nil).
  def self.strings_of(conf, keys)
    h = {}
    return h unless conf
    i = 0
    while i < keys.size
      v = conf[keys[i]]
      h[keys[i]] = v if v.is_a?(String)
      i += 1
    end
    h
  end

  # Writes config.toml from c (a Hash; the keys of CONFIG_KEYS that it has).
  def self.write_config(c)
    Dir.mkdir(DIR) unless Dir.exist?(DIR)
    File.open(CONFIG_FILE, "w") do |f|
      f.write(config_text(c))
    end
    nil
  end

  def self.config_text(c)
    out = "# This machine's Asterism settings (doc/ruby_asterism)\n".dup
    i = 0
    while i < CONFIG_KEYS.size
      k = CONFIG_KEYS[i]
      out << "#{k} = #{FmrbToml.dump_value(c[k].to_s)}\n" if c.key?(k)
      i += 1
    end
    out
  end

  # The files before CF1 -> config.toml, once. Returns what was copied ({}
  # when there was nothing). The old files stay (see the top of this file).
  def self.migrate
    c = {}
    found = []
    i = 0
    while i < OLD_FILES.size
      key, path, how = OLD_FILES[i]
      i += 1
      next unless File.exist?(path)
      found << path
      v = how == :line ? first_line(read_file(path)) : strings_of(FmrbToml.load(path), ["name"])["name"].to_s
      c[key] = v unless v.empty?
    end
    return c if found.empty?
    write_config(c)
    puts "asterism: copied #{found.join(', ')} into #{CONFIG_FILE}"
    c
  rescue => e
    puts "asterism: could not copy the old settings into #{CONFIG_FILE}: #{e.message}"
    c || {}
  end

  # The old files that are there although config.toml is (left by the copy,
  # or put by an old procedure): named in one log line, not read.
  def self.warn_old_files
    found = []
    i = 0
    while i < OLD_FILES.size
      path = OLD_FILES[i][1]
      found << path if File.exist?(path)
      i += 1
    end
    return if found.empty?
    puts "asterism: #{found.join(', ')} not read (#{CONFIG_FILE} is)"
  end

  # The file's text, or nil.
  def self.read_file(path)
    File.open(path, "r") { |f| f.read }
  rescue
    nil
  end

  def self.first_line(text)
    text.to_s.split("\n")[0].to_s.strip
  end

  # The top-level string key of /etc/system_conf.toml ("" without it).
  def self.system_conf(key)
    strings_of(FmrbToml.load(SYSTEM_CONF), [key])[key].to_s.strip
  end

  # The board's mDNS name ("" without WiFi).
  def self.hostname
    return "" unless Object.const_defined?(:FmrbApp)
    info = FmrbApp.wifi_info
    info ? info[:hostname].to_s : ""
  rescue
    ""
  end

  # How this machine connects, resolved (see the top of this file).
  class Settings
    attr_reader :router, :node, :name, :credentials, :error

    def initialize(router, node, name, credentials, error)
      @router = router
      @node = node
      @name = name
      @credentials = credentials
      @error = error
    end

    # The Zenoh configuration for Asterism::Zenoh::Session.open's config:
    # (the TLS keys with credentials, nil without).
    def zenoh_config
      @credentials ? @credentials.zenoh_config : nil
    end
  end

  # The settings of this machine (a Settings). Never raises: a credentials
  # file that is missing or cannot be read for a tls/ router is in error
  # (the application shows it when it connects).
  def self.settings
    existed = File.exist?(CONFIG_FILE)
    c = config
    warn_old_files if existed
    path = credentials_path
    router = c["router"].to_s
    router = system_conf("zenoh_router") if router.empty?
    creds = nil
    error = nil
    if router.empty? && path
      begin
        creds = Asterism::Credentials.load(path)
        router = creds.router.to_s
      rescue Asterism::Error => e
        error = e.message
      end
    end
    router = DEFAULT_ROUTER if router.empty?
    if creds.nil? && error.nil?
      begin
        creds = credentials_for(router)
      rescue Asterism::Error => e
        error = e.message
      end
    end
    node = c["node"].to_s
    node = hostname if node.empty?
    node = system_conf("asterism_node") if node.empty?
    node = FmrbConst::BOARD if node.empty?
    if creds && !tls?(router)
      creds = nil # a tar beside a router that is not tls/: not used
    end
    if creds && creds.name != node
      puts "asterism: the certificate's name #{creds.name} is the node ID (not #{node})"
      node = creds.name
    end
    Settings.new(router, node, c["name"].to_s, creds, error)
  end

  # The machine's Asterism credentials for a tls/ locator (connecting
  # straight to a cloud router with mutual TLS, doc/ruby_asterism G2a): the
  # .tar the console hands out, put in DIR as it is. One file; with more,
  # the first by name is used. devctl's /fs/get does not read this
  # directory (it holds the private key).
  CREDENTIALS_DIR = DIR

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
end

Asterism.profile(kind: :family_mruby, host: FmrbAsterism.host)
begin
  n = FmrbAsterism.saved_name
  Asterism.profile(name: n) unless n.empty?
rescue ArgumentError
  # a name the profile does not take (edited by hand): left out
end
