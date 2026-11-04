# frozen_string_literal: true
#
# build_tree.rb - borrow the checkout's build tree for a while and give it
# back (rake regress:size and rake regress:run, doc/regression/usage.md).
#
# Both checks build other targets than the one the developer has in build/
# (an ESP32 firmware per board, a Linux sim per family), and switching
# targets means `rake clean_all`. Rebuilding the developer's target at the
# end would cost another build, so the tree is moved aside instead and moved
# back afterwards, the way build.linux.saved/ is kept by hand (.gitignore):
#
#   build/                                  -> build.regress.saved/
#   sdkconfig                               -> sdkconfig.regress.saved
#   components/picoruby-esp32/picoruby/build/*  (the libmruby per chip)
#   main/prebuild_scripts/spinel/gen/*      (generated for one platform)
#   flash/etc/<generated>                   (system_conf, services, ...: the
#                                            sim reads flash/ directly)
#                                           -> build.regress-aside.saved/
#
# A run that is killed leaves the stash in place; the next run refuses to
# start until `ruby tools/regress/size.rb --restore` puts it back.
#
# Inside one rake regress:full, the same sim build is wanted by several parts
# (the standard Retro sim by run, screens and checkpoint; the standard Modern
# one by those and live). regress:full names a directory in
# REGRESS_BUILD_CACHE; then sim_build! takes a build made earlier in the run
# for the same options from the source tree as it is now (the key: the build
# options, HEAD, the tracked changes, the untracked files, .env and the
# mruby-checkpoint checkout), and keep_for_later! puts it back there once the
# part is done with it. Both move directories (no copy). A part run alone has
# no cache and builds as before.
require "digest"
require "fileutils"
require "json"
require "open3"

module Regress
  class BuildTree
    STASH = "build.regress.saved"
    STASH_SDK = "sdkconfig.regress.saved"
    STASH_ASIDE = "build.regress-aside.saved"
    PICORUBY_BUILD = "components/picoruby-esp32/picoruby/build"
    SPINEL_GEN = "main/prebuild_scripts/spinel/gen"
    GENERATED_ETC = %w[system_conf.toml system_conf.factory.toml services.toml associations.toml
                       secrets.toml wifi.toml].map { |f| "flash/etc/#{f}" }.freeze

    def initialize(core)
      @core = core
    end

    def path(rel) = File.join(@core, rel)

    def stashed? = File.exist?(path(STASH_ASIDE))

    # What build/ holds now, in a few words ("TAB5 (esp32p4)", "linux
    # (NARYAv3 sim)", "nothing").
    def describe(root = "build", sdk = "sdkconfig")
      cache = path("#{root}/CMakeCache.txt")
      return "nothing" unless File.exist?(cache)
      text = File.read(cache)
      hw = text[/^FMRB_HW_TARGET:\w+=(.*)$/, 1]
      family = text[/^FMRB_HW_FAMILY:\w+=(.*)$/, 1]
      chip = File.exist?(path(sdk)) ? File.read(path(sdk))[/^CONFIG_IDF_TARGET="(\w+)"/, 1] : nil
      chip ||= text[/^IDF_TARGET:\w+=(.*)$/, 1]
      if chip == "linux"
        "linux sim (#{family || 'retro'})"
      else
        "#{hw || 'NARYAv3'} (#{chip || '?'})"
      end
    end

    def stash!
      if stashed? || File.exist?(path(STASH))
        raise "a previous run left #{STASH}/ or #{STASH_ASIDE}/ behind (killed?). " \
              "Put it back first: ruby tools/regress/size.rb --restore"
      end
      @before = describe
      FileUtils.mkdir_p(path(STASH_ASIDE))
      File.write(path("#{STASH_ASIDE}/what.json"), JSON.generate("build" => @before))
      FileUtils.mv(path("build"), path(STASH)) if File.exist?(path("build"))
      FileUtils.mv(path("sdkconfig"), path(STASH_SDK)) if File.exist?(path("sdkconfig"))
      move_children(path(PICORUBY_BUILD), path("#{STASH_ASIDE}/picoruby_build"))
      move_children(path(SPINEL_GEN), path("#{STASH_ASIDE}/spinel_gen"))
      GENERATED_ETC.each do |f|
        next unless File.exist?(path(f))
        FileUtils.mkdir_p(path("#{STASH_ASIDE}/etc"))
        FileUtils.mv(path(f), path("#{STASH_ASIDE}/etc/#{File.basename(f)}"))
      end
      @before
    end

    # Clear what the borrowed builds left and put the stash back. Returns
    # what build/ holds afterwards.
    def restore!
      raise "nothing to restore (no #{STASH_ASIDE}/)" unless stashed?
      clean_all!
      FileUtils.rm_rf(Dir[path("#{SPINEL_GEN}/*")])
      GENERATED_ETC.each { |f| FileUtils.rm_f(path(f)) }
      FileUtils.mv(path(STASH), path("build")) if File.exist?(path(STASH))
      FileUtils.mv(path(STASH_SDK), path("sdkconfig")) if File.exist?(path(STASH_SDK))
      move_children(path("#{STASH_ASIDE}/picoruby_build"), path(PICORUBY_BUILD))
      move_children(path("#{STASH_ASIDE}/spinel_gen"), path(SPINEL_GEN))
      Dir[path("#{STASH_ASIDE}/etc/*")].each { |f| FileUtils.mv(f, path("flash/etc/#{File.basename(f)}")) }
      FileUtils.rm_rf(path(STASH_ASIDE))
      describe
    end

    # A borrowed build made a fresher copy of one libmruby the stash holds
    # (rake regress:screens builds the wasm one): it replaces the stashed
    # copy, so restore! hands the developer the current one.
    def keep_built!(child)
      from = path("#{PICORUBY_BUILD}/#{child}")
      return unless stashed? && File.directory?(from)
      to = path("#{STASH_ASIDE}/picoruby_build/#{child}")
      FileUtils.rm_rf(to)
      FileUtils.mkdir_p(File.dirname(to))
      FileUtils.mv(from, to)
    end

    CACHE_ENV = "REGRESS_BUILD_CACHE"

    def self.cache_dir
      d = ENV[CACHE_ENV].to_s
      d.empty? ? nil : d
    end

    # What a build of the sim depends on, besides its options: the source
    # tree as it is (the gem checkout too, when one is given).
    def source_fingerprint
      parts = [git("rev-parse", "HEAD"), git("diff", "HEAD", "--binary")]
      git("ls-files", "-o", "--exclude-standard", "-z").split("\0").sort.each do |f|
        st = File.stat(path(f)) rescue next # rubocop:disable Style/RescueModifier
        parts << "#{f} #{st.size} #{st.mtime.to_f}"
      end
      parts << (File.exist?(path(".env")) ? File.read(path(".env")) : "")
      ck = ENV["MRUBY_CHECKPOINT_DIR"].to_s
      unless ck.empty?
        parts << ck << git_in(ck, "rev-parse", "HEAD") << git_in(ck, "diff", "HEAD", "--binary")
      end
      Digest::SHA256.hexdigest(parts.join("\n"))
    end

    def build_key(env) = Digest::SHA256.hexdigest(JSON.generate([env.sort, source_fingerprint]))[0, 16]

    # clean_all, then the sim build for env: taken from the cache when this
    # run made it already, else `rake build:linux` (its output to log).
    # Returns [ok, seconds, cached].
    def sim_build!(env, log, rake: ENV["RAKE"] || "rake")
      clean_all!
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

    # The part is done with the sim build in build/ (its containers are
    # down): into the cache for the next part, when there is one.
    def keep_for_later!
      key = @cached_key
      @cached_key = nil
      dir = BuildTree.cache_dir
      return unless key && dir && File.exist?(path("build"))
      to = File.join(dir, key)
      return if File.exist?(to)
      FileUtils.mkdir_p(to)
      FileUtils.mv(path("build"), File.join(to, "build"))
      FileUtils.mv(path("sdkconfig"), File.join(to, "sdkconfig")) if File.exist?(path("sdkconfig"))
      move_children(path(PICORUBY_BUILD), File.join(to, "picoruby_build"))
      GENERATED_ETC.each do |f|
        next unless File.exist?(path(f))
        FileUtils.mkdir_p(File.join(to, "etc"))
        FileUtils.mv(path(f), File.join(to, "etc", File.basename(f)))
      end
    end

    # rake clean_all, without starting rake (the same three removals).
    def clean_all!
      FileUtils.rm_f(path("sdkconfig"))
      FileUtils.rm_rf(path("build"))
      FileUtils.rm_rf(Dir[path("#{PICORUBY_BUILD}/*")])
    end

    private

    def git(*args) = git_in(@core, *args)

    def git_in(dir, *args)
      out, = Open3.capture2("git", "-C", dir, *args)
      out
    end

    # A cached build back in place (build/ is empty: clean_all ran).
    def take_cached(from)
      return false unless File.directory?(File.join(from, "build"))
      FileUtils.mv(File.join(from, "build"), path("build"))
      FileUtils.mv(File.join(from, "sdkconfig"), path("sdkconfig")) if File.exist?(File.join(from, "sdkconfig"))
      move_children(File.join(from, "picoruby_build"), path(PICORUBY_BUILD))
      Dir[File.join(from, "etc", "*")].each { |f| FileUtils.mv(f, path("flash/etc/#{File.basename(f)}")) }
      FileUtils.rm_rf(from)
      true
    end

    def move_children(from, to)
      return unless File.directory?(from)
      FileUtils.mkdir_p(to)
      Dir.children(from).each { |c| FileUtils.mv(File.join(from, c), File.join(to, c)) }
    end
  end
end
