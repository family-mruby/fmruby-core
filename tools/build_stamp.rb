# frozen_string_literal: true
#
# build_stamp.rb - which sources a build was made from, and whether the tree
# still matches them.
#
# The Linux sim (build-linux/), the native program (native/build,
# native/build-mruby) and the browser build (wasm/build) each get a stamp
# (fmrb_build_stamp.json in the build directory) when they are built: the
# commit (HEAD) and, for every file that differs from it, the content's blob
# id. Before a start, the starters (tools/dev_run_check.sh, sim_up,
# fmrb_native.rb up, native_up, rake wasm:serve, web_up) compare it with the
# tree as it is now and refuse a stale build, naming the files that differ
# and the command that rebuilds it. STALE_OK=1 (allow_stale: true in the MCP
# tools) starts it anyway.
#
# What counts as a source: the files that go into the program (INPUTS
# below). flash/ is read live by the sim and the native program, so it is
# not theirs; the browser bundle packs the tracked flash/ files, so it is the
# browser's. Generated files (main/ruby/spinel/gen, fmrb_gen, the mrb
# bytecode, flash/etc, ...) are gitignored and never counted. Submodules count
# by their commit only: rake setup copies files into them on every build, and
# what it copies comes from lib/, which is an input.
#
# The comparison is by content, not by time: a file touched without a change,
# or a change committed after the build, is not stale. A committed change is
# found through `git diff <stamp HEAD> HEAD`.
#
# The regression tools key their reuse of a sim build on #digest
# (tools/regress/build_tree.rb), the same state in one hash.
#
# CLI:
#   ruby tools/build_stamp.rb check KIND [DIR]   exit 0 fresh, 3 stale (the reason on stderr)
#   ruby tools/build_stamp.rb write KIND [DIR]   stamp DIR with the tree as it is now
#   ruby tools/build_stamp.rb show  KIND [DIR]
# KIND: linux | native | native-mruby | web
require "digest"
require "json"
require "open3"
require "time"

module FmrbBuildStamp
  FILE = "fmrb_build_stamp.json"
  # The CLI's exit status for a stale build (1 would be any Ruby error).
  STALE_EXIT = 3
  VERSION = 1

  # Sources every firmware build compiles: the C, the Ruby, the signatures,
  # the gems and patches, the settings sources, the build scripts that turn
  # Ruby into C.
  FIRMWARE = %w[main components sig lib config cmake CMakeLists.txt
                tools/boundary tools/spinel].freeze

  KINDS = {
    "linux" => { dir: "build-linux", inputs: FIRMWARE, tracked_only: [],
                 name: "the Linux sim build", rebuild: "rake build:linux" },
    "native" => { dir: "native/build", inputs: FIRMWARE + %w[native hosted], tracked_only: [],
                  name: "the native program", rebuild: "rake native:core" },
    "native-mruby" => { dir: "native/build-mruby", inputs: FIRMWARE + %w[native hosted], tracked_only: [],
                        name: "the native program (mruby engine)",
                        rebuild: "NATIVE_ENGINE=mruby rake native:core" },
    # The bundle carries the tracked flash/ files (rake wasm:webflash), so
    # flash/ is the browser's source; its untracked files are not packed.
    # wasm/vendor holds fetched libraries, never edited here.
    "web" => { dir: "wasm/build", inputs: FIRMWARE + %w[wasm hosted :(exclude)wasm/vendor],
               tracked_only: %w[flash], name: "the browser build", rebuild: "rake wasm:web" },
  }.freeze

  module_function

  def kind!(kind)
    KINDS[kind.to_s] or raise ArgumentError, "unknown build kind #{kind.inspect} (#{KINDS.keys.join('|')})"
  end

  def default_dir(core, kind) = File.join(core, kind!(kind)[:dir])

  def stamp_path(dir) = File.join(dir, FILE)

  def git(core, *args, input: nil)
    out, st = Open3.capture2("git", "-C", core, *args, stdin_data: input.to_s, binmode: true)
    raise "git #{args.first} failed in #{core}" unless st.success?
    out
  end

  def pathspecs(kind, tracked: true)
    k = kind!(kind)
    tracked ? k[:inputs] + k[:tracked_only] : k[:inputs]
  end

  def submodule?(core, rel) = File.directory?(File.join(core, rel)) && File.exist?(File.join(core, rel, ".git"))

  # The tree as it is now: HEAD and the blob id of every input file that
  # differs from HEAD ("-" when deleted, a submodule's commit for a
  # submodule).
  def state(core, kind)
    head = git(core, "rev-parse", "HEAD").strip
    changed = git(core, "diff", "--name-only", "-z", "--ignore-submodules=dirty", "HEAD", "--",
                  *pathspecs(kind)).split("\0")
    untracked = git(core, "ls-files", "-o", "--exclude-standard", "-z", "--",
                    *pathspecs(kind, tracked: false)).split("\0")
    files = {}
    plain = []
    (changed + untracked).uniq.each do |f|
      full = File.join(core, f)
      if submodule?(core, f)
        files[f] = git(full, "rev-parse", "HEAD").strip
      elsif File.file?(full) || File.symlink?(full)
        plain << f
      else
        files[f] = "-"
      end
    end
    unless plain.empty?
      ids = git(core, "hash-object", "--stdin-paths", input: plain.join("\n") + "\n").split("\n")
      plain.zip(ids).each { |f, id| files[f] = id }
    end
    { "head" => head, "files" => files.sort.to_h }
  end

  # One hash of the state (the regression tools' build-cache key).
  def digest(core, kind) = Digest::SHA256.hexdigest(JSON.generate(state(core, kind)))

  def write(core, kind, dir = nil, state: nil, options: {})
    dir ||= default_dir(core, kind)
    st = state || state(core, kind)
    doc = { "version" => VERSION, "kind" => kind.to_s, "made_at" => Time.now.iso8601,
            "options" => options }.merge(st)
    Dir.exist?(dir) or raise "#{dir} does not exist"
    File.write(stamp_path(dir), JSON.pretty_generate(doc) + "\n")
    doc
  end

  def read(dir)
    JSON.parse(File.read(stamp_path(dir)))
  rescue Errno::ENOENT, JSON::ParserError
    nil
  end

  # blob ids of files at a commit ("-" when absent)
  def tree_ids(core, commit, files)
    ids = Hash.new("-")
    files.each_slice(500) do |slice|
      git(core, "ls-tree", "-z", commit, "--", *slice).split("\0").each do |line|
        meta, path = line.split("\t", 2)
        ids[path] = meta.split(" ")[2]
      end
    end
    ids
  end

  def commit?(core, sha)
    system("git", "-C", core, "cat-file", "-e", "#{sha}^{commit}", out: File::NULL, err: File::NULL)
  end

  # Compare the stamp in dir with the tree. Returns a hash:
  #   fresh: true/false, files: [paths that differ], reason: one line,
  #   stamp: the stamp (or nil)
  def check(core, kind, dir = nil)
    dir ||= default_dir(core, kind)
    k = kind!(kind)
    stamp = read(dir)
    unless stamp
      return { fresh: false, files: [], stamp: nil,
               reason: "#{k[:name]} in #{rel(core, dir)} has no stamp (#{FILE}): it was made " \
                       "before the stale-build guard, or the build did not finish" }
    end
    now = state(core, kind)
    old_files = stamp["files"] || {}
    candidates = (old_files.keys + now["files"].keys).uniq
    if stamp["head"] != now["head"]
      unless commit?(core, stamp["head"])
        return { fresh: false, files: [], stamp: stamp,
                 reason: "#{k[:name]} was made at commit #{stamp['head'][0, 12]}, which this repository no longer has" }
      end
      candidates |= git(core, "diff", "--name-only", "-z", "--ignore-submodules=dirty", stamp["head"], now["head"],
                        "--", *pathspecs(kind)).split("\0")
    end
    old_tree = stamp["head"] == now["head"] ? nil : tree_ids(core, stamp["head"], candidates)
    new_tree = tree_ids(core, now["head"], candidates)
    old_tree ||= new_tree
    differ = candidates.select do |f|
      (old_files[f] || old_tree[f]) != (now["files"][f] || new_tree[f])
    end
    differ = differ.sort_by { |f| -(File.mtime(File.join(core, f)).to_f rescue 0) } # rubocop:disable Style/RescueModifier
    if differ.empty?
      { fresh: true, files: [], stamp: stamp, reason: "#{k[:name]} matches the tree" }
    else
      { fresh: false, files: differ, stamp: stamp,
        reason: "#{k[:name]} in #{rel(core, dir)} (made #{stamp['made_at']} at " \
                "#{stamp['head'][0, 12]}) is older than #{differ.size} source file(s)" }
    end
  end

  def rel(core, dir)
    d = File.expand_path(dir)
    c = File.expand_path(core)
    d.start_with?("#{c}/") ? d[(c.size + 1)..] + "/" : d
  end

  # The refusal a starter prints: why, which files, how to rebuild, how to
  # go on anyway.
  def message(core, kind, result, override: "STALE_OK=1")
    k = kind!(kind)
    lines = ["stale build: #{result[:reason]}"]
    unless result[:files].empty?
      shown = result[:files].first(12)
      lines << "  newer than the build: #{shown.join(', ')}" \
               "#{result[:files].size > shown.size ? " (and #{result[:files].size - shown.size} more)" : ''}"
    end
    lines << "  rebuild: cd #{core} && #{k[:rebuild]}"
    lines << "  or start it as it is: #{override}"
    lines.join("\n")
  end

  # The starters' gate: nil when fresh (or overridden), the message otherwise.
  def refusal(core, kind, dir = nil, override: "STALE_OK=1", allow: ENV["STALE_OK"].to_s == "1")
    return nil if allow
    res = check(core, kind, dir)
    res[:fresh] ? nil : message(core, kind, res, override: override)
  end
end

if $PROGRAM_NAME == __FILE__
  cmd, kind, dir = ARGV
  core = File.expand_path("..", __dir__)
  unless %w[check write show].include?(cmd) && kind
    warn "usage: ruby tools/build_stamp.rb check|write|show KIND [DIR]  (KIND: #{FmrbBuildStamp::KINDS.keys.join('|')})"
    exit 2
  end
  dir = File.expand_path(dir) if dir
  case cmd
  when "check"
    res = FmrbBuildStamp.check(core, kind, dir)
    if res[:fresh]
      puts res[:reason]
    else
      warn FmrbBuildStamp.message(core, kind, res)
      exit FmrbBuildStamp::STALE_EXIT
    end
  when "write"
    doc = FmrbBuildStamp.write(core, kind, dir)
    puts "stamped #{kind}: #{doc['head'][0, 12]}, #{doc['files'].size} changed file(s)"
  when "show"
    puts JSON.pretty_generate(FmrbBuildStamp.read(dir || FmrbBuildStamp.default_dir(core, kind)))
  end
end
