#!/usr/bin/env ruby
# frozen_string_literal: true
#
# fmrb_send.rb - send a file from the PC to a Family mruby machine over
# Asterism (doc/asterism_file_transfer T2): the native program, the
# simulation, the browser build or a board, whichever node is named. The
# file goes as the kind "file" to <node>/transfer/inbox and is placed under
# that machine's /home/inbox/ (--sd: its SD card's /sd/inbox/; a machine
# with no card refuses it); it must receive (enabled = true in its
# receive.toml, or areceive on there) and take files from this PC's node
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
#        [--sd]                          to the SD card's /sd/inbox/ there
#        [--credentials ROW.tar]         connect with a member row's
#                                        certificate (the console's .tar):
#                                        its router and its name, unless
#                                        --router / --node say otherwise; a
#                                        cloud router's ACL then lets it call
#                                        the inboxes of whom chose it (T6, T8)
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
    opt = { router: DEFAULT_ROUTER, node: DEFAULT_NODE, overwrite: false, name: nil, sd: false }
    parser = OptionParser.new do |o|
      o.banner = "usage: fmrb_send.rb <node> <file> [--router LOC] [--node ID] [--overwrite] [--name NAME] [--sd] " \
                 "[--credentials ROW.tar]"
      o.on("--router LOC") { |v| opt[:router] = v }
      o.on("--node ID") { |v| opt[:node] = v }
      o.on("--overwrite") { opt[:overwrite] = true }
      o.on("--name NAME") { |v| opt[:name] = v }
      o.on("--sd") { opt[:sd] = true }
      o.on("--credentials TAR") { |v| opt[:credentials] = v }
    end
    given = argv.join(" ")
    parser.parse!(argv)
    abort parser.banner unless argv.size == 2
    to, path = argv
    abort "fmrb_send: no such file: #{path}" unless File.file?(path)
    load_asterism
    if opt[:credentials]
      args = given.include?("--router") ? [opt[:router]] : []
      kw = given.include?("--node") ? { node: opt[:node] } : {}
      net = Asterism.connect(*args, credentials: opt[:credentials], app: APP, **kw)
    else
      net = Asterism.connect(opt[:router], node: opt[:node], app: APP)
    end
    begin
      ok = send_file(net, to, path, overwrite: opt[:overwrite], name: opt[:name], sd: opt[:sd])
    ensure
      net.close
    end
    ok
  end

  # One file to <to>/transfer/inbox over net (an Asterism::Connection).
  # -> true when placed.
  def self.send_file(net, to, path, overwrite: false, name: nil, sd: false, out: $stdout)
    meta = overwrite ? { "overwrite" => true } : {}
    meta["inbox"] = "sd" if sd
    s = Asterism::Transfer::Sender.new("#{to}/transfer/inbox", "file", path, net: net,
                                                                              name: name || File.basename(path),
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
          net.poll
          break
        end
        net.poll
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
    note = "#{s.took_ms} ms, #{st['calls']} calls, pieces of #{s.piece}, #{s.window} at a time (the receiver's), " \
           "#{st['again']} again, #{st['retries']} retries"
    if s.ok? && s.answer["status"] == "placed"
      out.puts "fmrb_send: placed as #{s.answer['name']} in #{to}'s #{sd ? '/sd/inbox' : '/home/inbox'} (#{note})"
      true
    else
      why = s.ok? ? s.answer["reason"] : s.reason
      out.puts "fmrb_send: not sent: #{why} (#{note})"
      false
    end
  end
end

exit(FmrbSend.main(ARGV) ? 0 : 1) if $PROGRAM_NAME == __FILE__
