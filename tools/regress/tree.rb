# frozen_string_literal: true
#
# tree.rb - which tree of fmruby-core a regression tool runs from, where it
# finds the root tools, and the names its native programs, routers and
# servers take, so that two trees (the shared checkout and a worktree, or two
# worktrees) can run the same tool side by side (doc/regression/usage.md,
# "作業の木ごとの名前").
#
#   root     family-mruby: tools/ (fmrb_native.rb, fmrb_png.rb, ...) and the
#            sim's docker-compose.yml. The directory above this tree, or
#            FMRB_ROOT= (a worktree has no root tools above it: a directory
#            with a copy of tools/ whose fmruby-core is this tree).
#   names    the shared checkout (a clone: its .git is a directory) keeps the
#            names it always had. Any other tree (a worktree: .git is a
#            file) adds the first 6 hex digits of the SHA-1 of its real path
#            to run directories and container names, and takes its ports
#            from a block of its own: 20000 + 8 * slot, slot 1..400 from the
#            same digest. Clear of the developer's native program (5565), the
#            sim (5555), the screens' native program (5575), the compositor
#            checks (15565..15679) and the shared checkout's own ports.
#   ports    checked before use: one that answers or cannot be bound stops
#            the run with the reason (two trees whose digests share a slot,
#            or a program an earlier run left behind).
require "digest"
require "socket"

module Regress
  module Tree
    CORE = File.expand_path("../..", __dir__)
    ROOT = ENV["FMRB_ROOT"].to_s.empty? ? File.expand_path("..", CORE) : File.expand_path(ENV["FMRB_ROOT"])
    TOOLS = File.join(ROOT, "tools")

    # nil for the shared checkout, else 6 hex digits of this tree's path
    ID = File.directory?(File.join(CORE, ".git")) ? nil : Digest::SHA1.hexdigest(File.realpath(CORE))[0, 6]
    SLOT = ID ? ID.to_i(16) % 400 + 1 : 0
    PORT_BASE = 20_000
    # The ports of a tree's block, by use (a worktree's only).
    PORTS = {
      ckpt_a: 0, ckpt_b: 1, ckpt_c: 2, ckpt_router: 3, ckpt_router_ws: 4, ckpt_web: 5,
      screens_native: 6, screens_web: 7
    }.freeze

    class Busy < StandardError; end

    module_function

    def shared? = ID.nil?

    # A name of this tree's own: the shared checkout's as it was, another
    # tree's with its ID ("fmrb_regress_ckpt_zenohd" -> "..._zenohd_1a2b3c").
    def name(base, sep: "_") = ID ? "#{base}#{sep}#{ID}" : base

    # A run directory under /tmp: /tmp/<base>-<uid>, or /tmp/<base>-<uid>-<ID>.
    def run_dir(base) = name("/tmp/#{base}-#{Process.uid}", sep: "-")

    # The shared checkout's port, or this tree's port for that use.
    def port(shared_port, use)
      return shared_port if shared?
      PORT_BASE + SLOT * 8 + PORTS.fetch(use)
    end

    def label = ID ? "tree #{ID} (#{CORE})" : "the shared checkout"

    # Why the root tools cannot be used from here (nil: they can).
    def root_problem
      return nil if File.exist?(File.join(TOOLS, "fmrb_native.rb"))
      return "FMRB_ROOT=#{ENV['FMRB_ROOT']} has no tools/fmrb_native.rb" unless ENV["FMRB_ROOT"].to_s.empty?
      if File.file?(File.join(CORE, ".git"))
        "#{CORE} is a worktree of fmruby-core and has no root tools above it; give FMRB_ROOT=<a directory " \
          "with family-mruby's tools/ whose fmruby-core is this tree>"
      else
        "no root tools in #{TOOLS}; give FMRB_ROOT=<family-mruby>"
      end
    end

    def need_root!
      why = root_problem
      abort "regress: #{why}" if why
    end

    # Why the sim cannot be run from here (nil: it can). The sim's docker
    # compose (ROOT/docker-compose.yml) mounts ROOT/fmruby-core: it runs this
    # tree only when that is this tree.
    def sim_problem
      return root_problem if root_problem
      return "no docker-compose.yml in #{ROOT} (the sim's stack)" unless File.exist?(File.join(ROOT, "docker-compose.yml"))
      mounted = File.join(ROOT, "fmruby-core")
      same = File.exist?(mounted) && File.realpath(mounted) == File.realpath(CORE)
      return nil if same
      "the sim's docker compose mounts #{mounted}, not this tree (#{CORE}); the sim runs only the tree in " \
        "family-mruby (a worktree: leave the sim's parts out, or give FMRB_ROOT= whose fmruby-core is this tree)"
    end

    # Something answers on the port, or it cannot be bound here.
    def port_busy?(port)
      begin
        Socket.tcp("127.0.0.1", port, connect_timeout: 0.3).close
        return true
      rescue StandardError
        nil
      end
      begin
        TCPServer.new("127.0.0.1", port).close
        false
      rescue SystemCallError
        true
      end
    end

    # Raise Busy with the reason when the port is not free.
    def need_free_port!(port, what)
      return unless port_busy?(port)
      who = holder(port)
      raise Busy, "#{what}: port #{port} is taken#{who ? " (#{who})" : ''}; " \
                  "#{shared? ? 'an earlier run left a program behind, or someone uses it' : "another tree's port block may be this one (slot #{SLOT}), or an earlier run left a program behind"}"
    end

    # The process that listens on the port, when ss can tell.
    def holder(port)
      out = IO.popen(["ss", "-ltnpH", "sport = :#{port}"], err: File::NULL, &:read)
      out[/users:\(\("([^"]+)",pid=(\d+)/] ? "#{Regexp.last_match(1)} pid #{Regexp.last_match(2)}" : nil
    rescue StandardError
      nil
    end

    # The fmruby-native process of a run directory (its own pid file), when
    # it is alive and is one: a pid file outlives a program that crashed.
    def native_pid(run_dir)
      f = File.join(run_dir, "pid")
      return nil unless File.exist?(f)
      pid = File.read(f).strip.to_i
      return nil unless alive?(pid)
      cmd = File.binread("/proc/#{pid}/cmdline") rescue "" # rubocop:disable Style/RescueModifier
      cmd.include?("fmruby-native") ? pid : nil
    end

    # TERM, then KILL after 3 s.
    def stop_pid(pid)
      Process.kill("TERM", pid)
      30.times do
        return unless alive?(pid)
        sleep 0.1
      end
      Process.kill("KILL", pid)
    rescue SystemCallError
      nil
    end

    # A pid a tool wrote down is alive (one we may not signal counts).
    def alive?(pid)
      return false unless pid.to_i.positive?
      Process.kill(0, pid.to_i)
      true
    rescue Errno::ESRCH
      false
    rescue Errno::EPERM
      true
    end
  end
end
