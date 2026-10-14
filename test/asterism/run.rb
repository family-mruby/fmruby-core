#!/usr/bin/env ruby
# Host tests for FmrbAsterism's settings (CF1:
# lib/add/picoruby-fmrb-asterism/mrblib/fmrb-asterism.rb).
#
# Where a machine's router, node ID, name and credentials come from, and the
# one-time move of the files before CF1 into /home/asterism/config.toml. The
# gem file is plain Ruby over a few files, so the shipped file runs here with
# its paths moved into a temporary directory; the credentials are read by
# the real Asterism::Credentials (picoruby-asterism's credentials.rb) from a
# tar made here. Same tier as test/assoc/run.rb: no docker, no device.
#
# What this cannot see: the mDNS name (FmrbApp.wifi_info) and the network.
# Those stay with the simulator and the boards (doc/ruby_asterism/report/cf1.md).
#
#   rake asterism:config_test        (the asterism checkout's credentials.rb)
#   ruby test/asterism/run.rb        (the copy rake setup made)
#
# Not part of `rake test`, like asterism:test: it needs the asterism
# checkout, which CI does not fetch.

require "tmpdir"
require "fileutils"
require "json"
require "rubygems/package"
require "stringio"

module Check
  @failed = 0
  def self.failed = @failed
  def self.fail! = @failed += 1
end

def check(what, got, want)
  if got == want
    puts "  ok   #{what}"
  else
    puts "  FAIL #{what}: got #{got.inspect}, want #{want.inspect}"
    Check.fail!
  end
end

ROOT = File.expand_path("../..", __dir__)
GEM_FILE = "#{ROOT}/lib/add/picoruby-fmrb-asterism/mrblib/fmrb-asterism.rb"
CREDS_FILE = ENV["ASTERISM_CREDENTIALS_RB"] ||
             "#{ROOT}/components/picoruby-esp32/picoruby/mrbgems/picoruby-asterism/mrblib/credentials.rb"
TMP = Dir.mktmpdir("fmrb_asterism_test")
at_exit { FileUtils.rm_rf(TMP) }
HOME = "#{TMP}/home"
ETC = "#{TMP}/etc"

# What the gem file needs from the firmware: the board name and the profile.
module FmrbConst
  BOARD = "linux"
end

module Asterism
  class Error < StandardError; end
  @profile = {}
  def self.profile(**kw)
    @profile.merge!(kw)
    @profile
  end
end

unless File.exist?(CREDS_FILE)
  abort "#{CREDS_FILE} is missing: run rake asterism:config_test (or rake setup first)"
end
load CREDS_FILE

# The shipped file with its paths under TMP.
def load_gem
  src = File.read(GEM_FILE)
  src = src.gsub("\"/home/", "\"#{HOME}/").gsub("\"/etc/system_conf.toml\"", "\"#{ETC}/system_conf.toml\"")
  eval(src, TOPLEVEL_BINDING, GEM_FILE)
end

def reset
  FileUtils.rm_rf(HOME)
  FileUtils.rm_rf(ETC)
  FileUtils.mkdir_p(HOME)
  FileUtils.mkdir_p(ETC)
end

def put(path, text)
  FileUtils.mkdir_p(File.dirname(path))
  File.write(path, text)
end

# A credentials .tar like the console's (the key pair is not checked on
# the boards, so any PEM text does).
def put_tar(path, name, router)
  io = StringIO.new("".b)
  Gem::Package::TarWriter.new(io) do |tar|
    files = {
      "#{name}.key" => "-----BEGIN PRIVATE KEY-----\nk\n-----END PRIVATE KEY-----\n",
      "#{name}.pem" => "-----BEGIN CERTIFICATE-----\nc\n-----END CERTIFICATE-----\n",
      "ca.pem" => "-----BEGIN CERTIFICATE-----\nca\n-----END CERTIFICATE-----\n",
      "asterism.json" => JSON.generate({ "name" => name, "router" => router, "kind" => "node" })
    }
    files.each do |f, body|
      tar.add_file_simple("#{name}/#{f}", 0o600, body.bytesize) { |t| t.write(body) }
    end
  end
  FileUtils.mkdir_p(File.dirname(path))
  File.binwrite(path, io.string)
end

def quiet
  out = $stdout
  $stdout = StringIO.new
  yield
  $stdout.string
ensure
  $stdout = out
end

CONFIG = "#{HOME}/asterism/config.toml"
OLD_ROUTER = "#{HOME}/zenoh_echo.txt"
OLD_NODE = "#{HOME}/asterism_node.txt"
OLD_NAME = "#{HOME}/asterism_profile.toml"

reset
quiet { load_gem }

def settings
  s = nil
  log = quiet { s = FmrbAsterism.settings }
  [s, log]
end

puts "nothing written"
reset
s, = settings
check("the router is the sim's zenohd", s.router, "tcp/zenohd:7447")
check("the node is the board name", s.node, "linux")
check("there is no name", s.name, "")
check("no credentials", s.credentials, nil)
check("no error", s.error, nil)
check("no config.toml is made from nothing", File.exist?(CONFIG), false)

puts "config.toml"
reset
put(CONFIG, "# a comment\nrouter = \"tcp/192.0.2.2:7447\"\nnode = \"alice\"\nname = \"Ali \\\"A\\\"\"\n")
s, = settings
check("its router", s.router, "tcp/192.0.2.2:7447")
check("its node", s.node, "alice")
check("its name, unescaped", s.name, "Ali \"A\"")

puts "config.toml with only a name"
reset
put(CONFIG, "name = \"Bob\"\n")
s, = settings
check("the router falls back", s.router, "tcp/zenohd:7447")
check("the node falls back", s.node, "linux")

puts "the machine's settings (system_conf.toml)"
reset
put("#{ETC}/system_conf.toml", "zenoh_router = \"ws/localhost:7449\"\nasterism_node = \"wasm-abc123\"\n")
s, = settings
check("zenoh_router stands in for a router", s.router, "ws/localhost:7449")
check("asterism_node stands in for a node", s.node, "wasm-abc123")
put(CONFIG, "router = \"ws/192.0.2.9:7449\"\nnode = \"carol\"\n")
s, = settings
check("config.toml's router wins", s.router, "ws/192.0.2.9:7449")
check("config.toml's node wins", s.node, "carol")

puts "the files before CF1"
reset
put(OLD_ROUTER, "tls/203.0.113.5:7447\n")
put(OLD_NODE, "dave\nignored\n")
put(OLD_NAME, "# This machine's Asterism profile (written by the applications)\nname = \"Dave\"\n")
s, log = settings
check("the old router is read", s.router, "tls/203.0.113.5:7447")
check("there is no tar, so TLS is in error", s.error.to_s.include?("needs a credentials .tar"), true)
check("the old node is read", s.node, "dave")
check("the old name is read", s.name, "Dave")
check("config.toml is written", File.exist?(CONFIG), true)
check("with all three", FmrbAsterism.read_config,
      { "router" => "tls/203.0.113.5:7447", "node" => "dave", "name" => "Dave" })
check("the old files stay (older apps in storage read them)",
      [OLD_ROUTER, OLD_NODE, OLD_NAME].map { |f| File.exist?(f) }, [true, true, true])
check("the copy is logged", log.include?("copied"), true)
s, log = settings
check("read again: the same router", s.router, "tls/203.0.113.5:7447")
check("read again: nothing more is copied", log.include?("copied"), false)
check("read again: the old files are named once", log.scan("not read").size, 1)
File.write(OLD_ROUTER, "tcp/198.51.100.1:7447\n")
s, = settings
check("an old file changed later is not read", s.router, "tls/203.0.113.5:7447")

puts "only the old router file"
reset
put(OLD_ROUTER, "tcp/192.0.2.2:7447")
s, = settings
check("moved", FmrbAsterism.read_config, { "router" => "tcp/192.0.2.2:7447" })
check("the node is not written", s.node, "linux")

puts "an old file put after the move"
reset
put(CONFIG, "router = \"tcp/192.0.2.2:7447\"\n")
put(OLD_ROUTER, "tcp/198.51.100.1:7447\n")
s, log = settings
check("config.toml wins", s.router, "tcp/192.0.2.2:7447")
check("the old file is named in the log", log.include?("not read"), true)
check("and left where it is", File.exist?(OLD_ROUTER), true)

puts "the credentials tar"
reset
put_tar("#{HOME}/asterism/erin.tar", "erin", "tls/203.0.113.5:7447")
s, = settings
check("only the tar: its router", s.router, "tls/203.0.113.5:7447")
check("its name is the node", s.node, "erin")
check("the credentials are loaded", s.credentials && s.credentials.name, "erin")
check("their zenoh config has mTLS", s.zenoh_config && s.zenoh_config["transport/link/tls/enable_mtls"], true)
put(CONFIG, "node = \"frank\"\nname = \"Erin\"\n")
s, log = settings
check("the certificate's name beats config.toml's node", s.node, "erin")
check("which is logged", log.include?("certificate's name"), true)
check("the name stays the display name", s.name, "Erin")
put(CONFIG, "router = \"tls/198.51.100.7:7447\"\n")
s, = settings
check("config.toml's tls/ router beats the tar's", s.router, "tls/198.51.100.7:7447")
check("with the tar's credentials", s.credentials && s.credentials.name, "erin")
put(CONFIG, "router = \"tcp/192.0.2.2:7447\"\nnode = \"frank\"\n")
s, = settings
check("a tcp/ router leaves the tar unused", s.credentials, nil)
check("and the node is config.toml's", s.node, "frank")
put("#{HOME}/asterism/broken.tar", "not a tar")
FileUtils.rm_f(CONFIG)
s, = settings
check("a broken tar first by name is an error", s.error.to_s.empty?, false)

puts "the old router and a tar (the boards before CF1)"
reset
put(OLD_ROUTER, "tls/203.0.113.5:7447\n")
put_tar("#{HOME}/asterism/p4nano.tar", "p4nano", "tls/203.0.113.5:7447")
s, = settings
check("moved router", s.router, "tls/203.0.113.5:7447")
check("node from the certificate", s.node, "p4nano")
check("no error", s.error, nil)
check("the tar is kept", File.exist?("#{HOME}/asterism/p4nano.tar"), true)

puts "save_name"
reset
put(CONFIG, "router = \"tcp/192.0.2.2:7447\"\nnode = \"gina\"\n")
quiet { FmrbAsterism.save_name("Gina \"G\" \\") }
check("the name is written beside the others", FmrbAsterism.read_config,
      { "router" => "tcp/192.0.2.2:7447", "node" => "gina", "name" => "Gina \"G\" \\" })
check("and set in the profile", Asterism.profile[:name], "Gina \"G\" \\")
check("saved_name reads it", FmrbAsterism.saved_name, "Gina \"G\" \\")
quiet { FmrbAsterism.save_name("") }
check("an empty name removes it", FmrbAsterism.read_config, { "router" => "tcp/192.0.2.2:7447", "node" => "gina" })
reset
quiet { FmrbAsterism.save_name("Hana") }
check("save_name without config.toml makes it", FmrbAsterism.read_config, { "name" => "Hana" })
reset
put(OLD_ROUTER, "tcp/192.0.2.2:7447\n")
put(OLD_NAME, "name = \"Old\"\n")
quiet { FmrbAsterism.save_name("New") }
check("save_name moves the old files first", FmrbAsterism.read_config,
      { "router" => "tcp/192.0.2.2:7447", "name" => "New" })

puts
if Check.failed > 0
  puts "#{Check.failed} FAILED"
  exit 1
end
puts "all passed"
