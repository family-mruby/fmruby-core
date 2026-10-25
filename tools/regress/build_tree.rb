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
require "fileutils"
require "json"

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

    # rake clean_all, without starting rake (the same three removals).
    def clean_all!
      FileUtils.rm_f(path("sdkconfig"))
      FileUtils.rm_rf(path("build"))
      FileUtils.rm_rf(Dir[path("#{PICORUBY_BUILD}/*")])
    end

    private

    def move_children(from, to)
      return unless File.directory?(from)
      FileUtils.mkdir_p(to)
      Dir.children(from).each { |c| FileUtils.mv(File.join(from, c), File.join(to, c)) }
    end
  end
end
