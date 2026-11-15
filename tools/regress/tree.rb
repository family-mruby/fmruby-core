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
#            checks (15565..15681) and the shared checkout's own ports.
#   ports    the default (above) when it is free, else a free one of
#            24000..24999, said on stderr; marked as the process's own in
#            /tmp/fmrb-regress-ports-<uid> until it exits, so parallel jobs
#            pass each other's by. Checked again before use: one that
#            answers or cannot be bound then stops the run with the reason.
#            Tools whose port is the same in every tree (test/native/'s
#            checks) take it with free_port(default, use) the same way.
require "digest"
require "fileutils"
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

    # The shared checkout's port, or this tree's port for that use -- when it
    # is free. When something answers on it, it cannot be bound, or another
    # job holds its mark (below), the first free port of FALLBACK is taken
    # instead and the run says so. Either way the port is marked as this
    # process's until it exits, so a job started meanwhile (another tool, the
    # same tool twice) passes it by.
    def port(shared_port, use)
      want = shared? ? shared_port : PORT_BASE + SLOT * 8 + PORTS.fetch(use)
      claim_port(want, use)
    end

    # A port that is the same in every tree (a check that runs one at a time
    # per tree, e.g. test/native/'s debug ports): `default` when it is free,
    # else a free one of FALLBACK, marked like port() above. Two trees (or a
    # tree and a leftover program) then pass each other by instead of
    # failing on a taken port.
    def free_port(default, use) = claim_port(default, use)

    # --- port marks ---------------------------------------------------------
    #
    # One file per port in LOCK_DIR, made with O_EXCL: "<pid> <use> <tree>".
    # A mark whose pid is gone is stale and taken over. A process removes its
    # own marks when it exits (not a forked child's exit: the pid is checked).
    LOCK_DIR = "/tmp/fmrb-regress-ports-#{Process.uid}"
    # Above the worktrees' blocks (20000..23207) and clear of the ports the
    # tools and the developer use (5555.., 8006.., 15565.., 17447..).
    FALLBACK = (24_000..24_999).freeze

    @claimed = {}
    @mine = []

    class << self
      attr_reader :claimed, :mine
    end

    def claim_port(want, use)
      key = "#{use}:#{want}"
      return Tree.claimed[key] if Tree.claimed[key]
      ([want] + FALLBACK.to_a).each do |p|
        next if p != want && Tree.claimed.value?(p)
        next unless mark_port(p, use)
        if port_busy?(p)
          unmark_port(p)
          next
        end
        unless p == want
          who = holder(want) || marked_by(want)
          warn "regress: port #{want} (#{use}) is taken#{who ? " by #{who}" : ''}; using #{p}"
        end
        Tree.claimed[key] = p
        return p
      end
      raise Busy, "no free port for #{use} (#{want}, #{FALLBACK})"
    end

    def lock_path(port) = File.join(LOCK_DIR, "#{port}.lock")

    # true when the mark is now this process's
    def mark_port(port, use)
      FileUtils.mkdir_p(LOCK_DIR)
      2.times do
        begin
          File.open(lock_path(port), File::WRONLY | File::CREAT | File::EXCL) do |f|
            f.write("#{Process.pid} #{use} #{label}\n")
          end
          Tree.mine << port
          return true
        rescue Errno::EEXIST
          pid = File.read(lock_path(port)).split.first.to_i rescue 0 # rubocop:disable Style/RescueModifier
          return true if pid == Process.pid
          return false if alive?(pid)
          FileUtils.rm_f(lock_path(port)) # stale: its process is gone
        end
      end
      false
    end

    def unmark_port(port)
      FileUtils.rm_f(lock_path(port)) if Tree.mine.delete(port)
    end

    # who holds a port's mark ("pid N (use, tree)"), when someone does
    def marked_by(port)
      pid, use, *tree = File.read(lock_path(port)).split
      alive?(pid) ? "pid #{pid} (#{use}, #{tree.join(' ')})" : nil
    rescue SystemCallError
      nil
    end

    OWNER = Process.pid
    at_exit do
      if Process.pid == OWNER
        Tree.mine.dup.each do |p|
          pid = File.read(Tree.lock_path(p)).split.first.to_i rescue 0 # rubocop:disable Style/RescueModifier
          FileUtils.rm_f(Tree.lock_path(p)) if pid == OWNER
        end
      end
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
