# frozen_string_literal: true
#
# build_tree.rb - borrow the checkout's build tree for a while and give it
# back (rake regress:size, regress:run, regress:screens, ...;
# doc/regression/usage.md).
#
# The ESP32 firmware and the Linux sim build in places of their own
# (rakelib/build.rake), so a check borrows only the one it builds:
#
#   BuildTree.new(core)          the ESP32 build (rake regress:size, :boot)
#     build/                                -> build.regress.saved/
#     sdkconfig                             -> sdkconfig.regress.saved
#     picoruby/build/{esp32,esp32p4,ckpt-table-*}  (libmruby per chip)
#     main/ruby/spinel/gen/*                (generated for one platform)
#                                           -> build.regress-aside.saved/
#   BuildTree.new(core, :linux)  the Linux sim build (the sim parts)
#     build-linux/ (its sdkconfig inside)   -> build-linux.regress.saved/
#     picoruby/build/family-mruby-linux, main/ruby/spinel/gen/*,
#     flash/etc/<generated>                 (system_conf, services, ...: the
#                                            sim reads flash/ directly)
#                                           -> build-linux.regress-aside.saved/
#
# The other builds (the browser's and the native program's libmruby among
# them) are left where they are.
#
# A run that is killed leaves the stash in place; the next run refuses to
# start until `ruby tools/regress/size.rb --restore` puts it back (both
# kinds).
#
# Inside one rake regress:full, the same sim build is wanted by several parts
# (the standard Retro sim by run, screens and checkpoint; the standard Modern
# one by those and live). regress:full names a directory in
# REGRESS_BUILD_CACHE; then sim_build! takes a build made earlier in the run
# for the same options from the source tree as it is now (the key: the build
# options, the sim's sources as tools/build_stamp.rb sees them -- the same
# state the stale-build guard compares --, .env and the mruby-checkpoint
# checkout), and keep_for_later! puts it back there once the part is done
# with it. Both move directories (no copy). A part run alone has no cache and
# builds as before.
require "digest"
require "fileutils"
require "json"
require "open3"
require_relative "../build_stamp"

module Regress
  class BuildTree
    PICORUBY_BUILD = "components/picoruby-esp32/picoruby/build"
    SPINEL_GEN = "main/ruby/spinel/gen"
    GENERATED_ETC = %w[system_conf.toml system_conf.factory.toml services.toml associations.toml
                       secrets.toml wifi.toml].map { |f| "flash/etc/#{f}" }.freeze
    LINUX_ROOT = "build-linux"

    KINDS = {
      esp32: { root: "build", sdk: "sdkconfig", stash: "build.regress.saved",
               stash_sdk: "sdkconfig.regress.saved", aside: "build.regress-aside.saved",
               mruby: %w[esp32 esp32p4 ckpt-table-*], etc: [] },
      linux: { root: LINUX_ROOT, sdk: nil, stash: "build-linux.regress.saved",
               stash_sdk: nil, aside: "build-linux.regress-aside.saved",
               mruby: %w[family-mruby-linux], etc: GENERATED_ETC },
    }.freeze

    # The ESP32 kind's names, as the tools' messages have always said them.
    STASH = KINDS[:esp32][:stash]
    STASH_SDK = KINDS[:esp32][:stash_sdk]
    STASH_ASIDE = KINDS[:esp32][:aside]

    attr_reader :kind

    def initialize(core, kind = :esp32)
      @core = core
      @kind = kind
      @k = KINDS.fetch(kind)
    end

    def path(rel) = File.join(@core, rel)

    def root = @k[:root]
    def elf = path("#{root}/fmruby-core.elf")

    def stashed? = File.exist?(path(@k[:aside]))

    # The build kinds with a stash left behind.
    def self.stashed_kinds(core) = KINDS.keys.select { |k| new(core, k).stashed? }

    # What the build directory holds now, in a few words ("TAB5 (esp32p4)",
    # "linux sim (retro)", "nothing").
    def describe
      cache = path("#{root}/CMakeCache.txt")
      return "nothing" unless File.exist?(cache)
      text = File.read(cache)
      hw = text[/^FMRB_HW_TARGET:\w+=(.*)$/, 1]
      family = text[/^FMRB_HW_FAMILY:\w+=(.*)$/, 1]
      sdk = path(@k[:sdk] || "#{root}/sdkconfig")
      chip = File.exist?(sdk) ? File.read(sdk)[/^CONFIG_IDF_TARGET="(\w+)"/, 1] : nil
      chip ||= text[/^IDF_TARGET:\w+=(.*)$/, 1]
      if chip == "linux"
        "linux sim (#{family || 'retro'})"
      else
        "#{hw || 'NARYAv3'} (#{chip || '?'})"
      end
    end

    def stash!
      if stashed? || File.exist?(path(@k[:stash]))
        raise "a previous run left #{@k[:stash]}/ or #{@k[:aside]}/ behind (killed?). " \
              "Put it back first: ruby tools/regress/size.rb --restore"
      end
      @before = describe
      aside = path(@k[:aside])
      FileUtils.mkdir_p(aside)
      File.write(File.join(aside, "what.json"), JSON.generate(root => @before))
      FileUtils.mv(path(root), path(@k[:stash])) if File.exist?(path(root))
      FileUtils.mv(path(@k[:sdk]), path(@k[:stash_sdk])) if @k[:sdk] && File.exist?(path(@k[:sdk]))
      mruby_dirs.each { |d| move(d, File.join(aside, "picoruby_build", File.basename(d))) }
      move_children(path(SPINEL_GEN), File.join(aside, "spinel_gen"))
      @k[:etc].each do |f|
        next unless File.exist?(path(f))
        FileUtils.mkdir_p(File.join(aside, "etc"))
        FileUtils.mv(path(f), File.join(aside, "etc", File.basename(f)))
      end
      @before
    end

    # Clear what the borrowed builds left and put the stash back. Returns
    # what the build directory holds afterwards.
    def restore!
      raise "nothing to restore (no #{@k[:aside]}/)" unless stashed?
      aside = path(@k[:aside])
      clean!
      FileUtils.rm_rf(Dir[path("#{SPINEL_GEN}/*")])
      @k[:etc].each { |f| FileUtils.rm_f(path(f)) }
      FileUtils.mv(path(@k[:stash]), path(root)) if File.exist?(path(@k[:stash]))
      FileUtils.mv(path(@k[:stash_sdk]), path(@k[:sdk])) if @k[:sdk] && File.exist?(path(@k[:stash_sdk]))
      move_children(File.join(aside, "picoruby_build"), path(PICORUBY_BUILD))
      move_children(File.join(aside, "spinel_gen"), path(SPINEL_GEN))
      Dir[File.join(aside, "etc", "*")].each { |f| FileUtils.mv(f, path("flash/etc/#{File.basename(f)}")) }
      FileUtils.rm_rf(aside)
      describe
    end

    # A borrowed build made a fresher copy of one libmruby the stash holds:
    # it replaces the stashed copy, so restore! hands the developer the
    # current one. Only this kind's libmruby builds are stashed (the
    # browser's and the native program's stay in place), so for those it
    # has nothing to do.
    def keep_built!(child)
      from = path("#{PICORUBY_BUILD}/#{child}")
      to = path("#{@k[:aside]}/picoruby_build/#{child}")
      return unless stashed? && File.directory?(from) && File.exist?(to)
      FileUtils.rm_rf(to)
      FileUtils.mv(from, to)
    end

    CACHE_ENV = "REGRESS_BUILD_CACHE"

    def self.cache_dir
      d = ENV[CACHE_ENV].to_s
      d.empty? ? nil : d
    end

    # What a build of the sim depends on, besides its options: the sim's
    # sources (tools/build_stamp.rb), .env, and the gem checkout when one is
    # given.
    def source_fingerprint
      parts = [FmrbBuildStamp.digest(@core, :linux)]
      parts << (File.exist?(path(".env")) ? File.read(path(".env")) : "")
      ck = ENV["MRUBY_CHECKPOINT_DIR"].to_s
      unless ck.empty?
        parts << ck << git_in(ck, "rev-parse", "HEAD") << git_in(ck, "diff", "HEAD", "--binary")
      end
      Digest::SHA256.hexdigest(parts.join("\n"))
    end

    def build_key(env) = Digest::SHA256.hexdigest(JSON.generate([env.sort, source_fingerprint]))[0, 16]

    # A clean Linux build, then the sim build for env: taken from the cache
    # when this run made it already, else `rake build:linux` (its output to
    # log). Returns [ok, seconds, cached].
    def sim_build!(env, log, rake: ENV["RAKE"] || "rake")
      raise "sim_build! is for the Linux kind (BuildTree.new(core, :linux))" unless kind == :linux
      clean!
      dir = BuildTree.cache_dir
      key = dir && build_key(env)
      if key && take_cached(File.join(dir, key))
        File.write(log, "build:linux: the build made earlier in this run (#{key}), from #{dir}\n")
        @cached_key = key
        return [true, 0.0, true]
      end
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      ok = File.open(log, "w") { |f| system(env, rake, "build:linux", chdir: @core, out: f, err: f) }
      @cached_key = ok ? key : nil
      [ok, Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0, false]
    end

    # The part is done with the sim build (its containers are down): into
    # the cache for the next part, when there is one.
    def keep_for_later!
      key = @cached_key
      @cached_key = nil
      dir = BuildTree.cache_dir
      return unless key && dir && File.exist?(path(root))
      to = File.join(dir, key)
      return if File.exist?(to)
      FileUtils.mkdir_p(to)
      FileUtils.mv(path(root), File.join(to, "build"))
      mruby_dirs.each { |d| move(d, File.join(to, "picoruby_build", File.basename(d))) }
      @k[:etc].each do |f|
        next unless File.exist?(path(f))
        FileUtils.mkdir_p(File.join(to, "etc"))
        FileUtils.mv(path(f), File.join(to, "etc", File.basename(f)))
      end
    end

    # This kind's clean: rake clean_all for the ESP32 build, rake clean:linux
    # for the sim (without starting rake).
    def clean!
      FileUtils.rm_f(path(@k[:sdk])) if @k[:sdk]
      FileUtils.rm_rf(path(root))
      mruby_dirs.each { |d| FileUtils.rm_rf(d) }
    end

    # The ESP32 build's clean (rake clean_all), as the board tools call it.
    def clean_all!
      raise "clean_all! is the ESP32 kind's clean" unless kind == :esp32
      clean!
    end

    private

    def mruby_dirs = @k[:mruby].flat_map { |g| Dir[path("#{PICORUBY_BUILD}/#{g}")] }

    def git_in(dir, *args)
      out, = Open3.capture2("git", "-C", dir, *args)
      out
    end

    # A cached build back in place (the build directory is empty: clean!
    # ran).
    def take_cached(from)
      return false unless File.directory?(File.join(from, "build"))
      FileUtils.mv(File.join(from, "build"), path(root))
      move_children(File.join(from, "picoruby_build"), path(PICORUBY_BUILD))
      Dir[File.join(from, "etc", "*")].each { |f| FileUtils.mv(f, path("flash/etc/#{File.basename(f)}")) }
      FileUtils.rm_rf(from)
      true
    end

    def move(from, to)
      return unless File.exist?(from)
      FileUtils.mkdir_p(File.dirname(to))
      FileUtils.mv(from, to)
    end

    def move_children(from, to)
      return unless File.directory?(from)
      FileUtils.mkdir_p(to)
      Dir.children(from).each { |c| FileUtils.mv(File.join(from, c), File.join(to, c)) }
    end
  end
end
