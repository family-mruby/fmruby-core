#!/usr/bin/env ruby
# frozen_string_literal: true
#
# fmrb_send.rb - send a file from the PC to a Family mruby machine over
# Asterism (doc/asterism_file_transfer T2): the native program, the
# simulation, the browser build or a board, whichever node is named. The
# file goes as the kind "file" to <node>/transfer/inbox and is placed under
# that machine's /home/inbox/; it must take files from this PC's node
# ([file] from in its /home/asterism/receive.toml).
#
#   ruby tools/fmrb_send.rb <node> <file>
#        [--router tcp/127.0.0.1:7447]   the router that machine is on
#        [--node fmrb-send]              this PC's node ID (the receiver's
#                                        receive.toml names it)
#        [--overwrite]                   replace a file of that name there
#                                        (default: it stays, and this one
#                                        gets a name of its own, foo(1).txt)
#        [--name NAME]                   the name there (default: the file's)
#
# It prints the progress and the name the file was placed under; the exit
# status is 0 when placed. Ctrl+C stops it, and the machine drops what it
# holds of the file. A dropped connection or a receiver that stops for a
# while is gone on from where the receiver stands (Asterism::Transfer, the
# same code as the machines').
#
# Asterism (CRuby) is taken from ASTERISM_DIR (a working checkout of the
# gem), else the pinned one of this tree (vendor/asterism, rake
# asterism:setup), else the installed gem; asterism-zenoh is the installed
# gem.
require "optparse"

module FmrbSend
  CORE = File.expand_path("..", __dir__)
  DEFAULT_ROUTER = "tcp/127.0.0.1:7447"
  DEFAULT_NODE = "fmrb-send"
  APP = "fmrb_send"

  def self.load_asterism
    [ENV["ASTERISM_DIR"], File.join(CORE, "vendor", "asterism")].compact.each do |d|
      lib = File.join(d, "lib")
      next unless File.exist?(File.join(lib, "asterism", "transfer.rb"))
      $LOAD_PATH.unshift(lib)
      break
    end
    require "asterism"
    require "asterism/transfer"
  end

  def self.main(argv)
    opt = { router: DEFAULT_ROUTER, node: DEFAULT_NODE, overwrite: false, name: nil }
    parser = OptionParser.new do |o|
      o.banner = "usage: fmrb_send.rb <node> <file> [--router LOC] [--node ID] [--overwrite] [--name NAME]"
      o.on("--router LOC") { |v| opt[:router] = v }
      o.on("--node ID") { |v| opt[:node] = v }
      o.on("--overwrite") { opt[:overwrite] = true }
      o.on("--name NAME") { |v| opt[:name] = v }
    end
    parser.parse!(argv)
    abort parser.banner unless argv.size == 2
    to, path = argv
    abort "fmrb_send: no such file: #{path}" unless File.file?(path)
    load_asterism
    Asterism.connect(opt[:router], node: opt[:node], app: APP)
    begin
      ok = send_file(to, path, overwrite: opt[:overwrite], name: opt[:name])
    ensure
      Asterism.close
    end
    ok
  end

  # One file to <to>/transfer/inbox, connected already. -> true when placed.
  def self.send_file(to, path, overwrite: false, name: nil, out: $stdout)
    meta = overwrite ? { "overwrite" => true } : {}
    s = Asterism::Transfer::Sender.new("#{to}/transfer/inbox", "file", path, name: name || File.basename(path),
                                                                              meta: meta)
    s.logger = nil
    out.puts "fmrb_send: #{path} (#{s.size} bytes) to #{to}"
    shown = 0
    stop = false
    old = trap("INT") { stop = true }
    begin
      until s.done?
        if stop
          s.cancel
          Asterism.poll
          break
        end
        Asterism.poll
        s.step
        q = s.size.zero? ? 4 : s.pos * 4 / s.size
        if q > shown
          shown = q
          out.puts format("fmrb_send: %d of %d bytes", s.pos, s.size) if q < 4
        end
        sleep 0.0005
      end
    ensure
      trap("INT", old || "DEFAULT")
    end
    st = s.stats
    note = "#{s.took_ms} ms, #{st['calls']} calls, #{st['again']} again, #{st['retries']} retries"
    if s.ok? && s.answer["status"] == "placed"
      out.puts "fmrb_send: placed as #{s.answer['name']} in #{to}'s /home/inbox (#{note})"
      true
    else
      why = s.ok? ? s.answer["reason"] : s.reason
      out.puts "fmrb_send: not sent: #{why} (#{note})"
      false
    end
  end
end

exit(FmrbSend.main(ARGV) ? 0 : 1) if $PROGRAM_NAME == __FILE__
