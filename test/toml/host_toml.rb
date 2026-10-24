# FmrbToml under host Ruby, for the host tests (test/toml, services, assoc,
# asterism).
#
# The Ruby half is the shipped file (mrblib/fmrb-toml.rb). The C half,
# FmrbToml._parse, is played by test/toml/dump.c: tomlc99 and the firmware's
# own walk (ports/esp32/toml_walk.c) built with the host cc, printing the
# Hash as a Ruby literal. Only the step that makes mruby objects is not the
# board's.
#
#   require_relative "../toml/host_toml"

require "open3"
require "fileutils"

module HostToml
  ROOT = File.expand_path("../..", __dir__)
  BUILD = File.join(ROOT, "test/toml/build")
  BIN = File.join(BUILD, "toml_dump")
  TOMLC99 = File.join(ROOT, "components/toml_parser/tomlc99")
  GEM_PORT = File.join(ROOT, "lib/add/picoruby-fmrb-app/ports/esp32")
  SRCS = [File.join(ROOT, "test/toml/dump.c"), File.join(GEM_PORT, "toml_walk.c"),
          File.join(TOMLC99, "toml.c")]

  def self.build
    return if File.exist?(BIN) && SRCS.all? { |s| File.mtime(s) <= File.mtime(BIN) }
    FileUtils.mkdir_p(BUILD)
    cmd = ["cc", "-std=c11", "-O1", "-Wall", "-I#{TOMLC99}", "-I#{GEM_PORT}", "-o", BIN, *SRCS, "-lm"]
    system(*cmd) or abort("host_toml: building #{BIN} failed")
  end
end

HostToml.build

unless defined?(Log)
  module Log
    @lines = []
    class << self
      attr_reader :lines
    end
    def self.warn(m) = @lines << m
    def self.info(m) = nil
    def self.error(m) = @lines << m
    def self.debug(m) = nil
  end
end

module FmrbToml
  def self._parse(text)
    raise ArgumentError, "string contains null byte" if text.include?("\0")
    out, status = Open3.capture2(HostToml::BIN, stdin_data: text.b)
    raise "toml_dump failed" unless status.success?
    return out[4..] if out.start_with?("ERR:")
    eval(out.force_encoding("UTF-8"))
  end
end

load File.join(HostToml::ROOT, "lib/add/picoruby-fmrb-app/mrblib/fmrb-toml.rb")
