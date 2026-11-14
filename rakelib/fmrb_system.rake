# The machine's settings and services moved from FmrbApp to FmrbSystem
# (doc/codebase_tidy T5): Wi-Fi, BLE, power, the clock, the audio settings, the
# PC's settings. An mruby program calling one of them on FmrbApp compiles and
# only fails with NoMethodError when that line runs, which may be a menu item
# nobody opens in a test. `rake system:check` (part of `rake test`, so of
# `rake regress`) finds such calls before that:
#   - the names are the ones the mruby binding registers on FmrbSystem
#     (lib/add/picoruby-fmrb-app/ports/esp32/app.c), so a method moved later is
#     covered without editing this file;
#   - no `FmrbApp.<name>` (nor `FmrbApp.respond_to?(:<name>)`) for one of them
#     in the tracked sources under flash/, main/, lib/, tools/, tool/,
#     native/, test/ and sig/;
#   - the Spinel FmrbSystem (main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb)
#     and sig/fmrb_system.rbs name the same methods as the mruby binding (the
#     native program's own ones excepted where the mruby binding has them
#     only there).

require "open3"

namespace :system do
  SYSTEM_C = "lib/add/picoruby-fmrb-app/ports/esp32/app.c"
  SYSTEM_SPINEL = "main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb"
  SYSTEM_SIG = "sig/fmrb_system.rbs"
  SYSTEM_DIRS = %w[flash main lib tools tool native test sig].freeze

  desc "Check that no source calls a FmrbSystem method on FmrbApp, and that the engines and sig agree"
  task :check do
    errors = []
    src = File.read(SYSTEM_C)
    names = []
    native_only = []
    in_native = false
    src.each_line do |l|
      in_native = true if l.start_with?("#if defined(FMRB_PLATFORM_NATIVE)")
      in_native = false if l.start_with?("#endif") || l.start_with?("#else")
      next unless l =~ /mrb_define_class_method\(mrb, sys_module, "([^"]+)"/
      names << $1
      native_only << $1 if in_native
    end
    errors << "#{SYSTEM_C}: no FmrbSystem methods found" if names.empty?

    alt = names.map { |n| Regexp.escape(n) }.join("|")
    re = /FmrbApp\.(?:(?:#{alt})(?![\w?!])|respond_to\?\(:(?:#{alt})\))/
    files, = Open3.capture2("git", "ls-files", "--", *SYSTEM_DIRS)
    hits = []
    files.lines.map(&:chomp).each do |f|
      next if f == "rakelib/fmrb_system.rake" || !File.file?(f)
      next unless f =~ /\.(rb|c|h|rbs|py|toml|cmake|txt)\z/
      File.foreach(f).with_index(1) do |line, no|
        hits << "#{f}:#{no}: #{line.strip}" if line.valid_encoding? && line =~ re
      end
    end
    unless hits.empty?
      errors << "FmrbApp.<name> for a FmrbSystem method (#{hits.size}):\n  " + hits.first(20).join("\n  ")
    end

    spinel = File.read(SYSTEM_SPINEL)
    mod = spinel[/^module FmrbSystem\n(.*?)^end\n/m, 1].to_s
    sp_names = mod.scan(/^  def self\.([\w?!]+)/).flatten
    missing = names - native_only - sp_names
    extra = sp_names - names
    errors << "Spinel FmrbSystem lacks: #{missing.join(' ')}" unless missing.empty?
    errors << "Spinel FmrbSystem has what the mruby binding has not: #{extra.join(' ')}" unless extra.empty?

    if File.exist?(SYSTEM_SIG)
      sig_names = File.read(SYSTEM_SIG).scan(/^\s*def self\.([\w?!]+)/).flatten
      missing = names - sig_names
      errors << "#{SYSTEM_SIG} lacks: #{missing.join(' ')}" unless missing.empty?
      extra = sig_names - names
      errors << "#{SYSTEM_SIG} names what FmrbSystem has not: #{extra.join(' ')}" unless extra.empty?
    else
      errors << "#{SYSTEM_SIG} is missing"
    end

    abort "system:check failed:\n" + errors.join("\n") unless errors.empty?
    puts "system:check: #{names.size} FmrbSystem methods, no FmrbApp.<name> calls left"
  end
end
