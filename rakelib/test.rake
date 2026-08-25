# rakelib/test.rake
# Test entry points: the native host suites, and the opt-in sim tier.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.

# ---- Tests -------------------------------------------------------------------
# Two tiers, kept apart on purpose:
#   * host tests (this `test` task) -- native g++/gcc/ruby/python, no docker,
#     no device. Fast; this is what CI runs and what to run after a code change.
#   * sim/integration -- the docker Linux build driven headless by
#     tools/dev_run_check.sh (+ fmrb_screenshot / fmrb_input). Heavier; run it
#     when a change could affect on-screen behaviour.
#
# Lints (spinel:doctor) are separate again: they need the Spinel compiler
# checkout and report style/inference issues, not pass/fail like these suites.
desc "Run all native host test suites (FmrbUI + services + assoc + picoruby-ti + BASIC + MicroPython)"
task :test => ["ui:test", "services:test", "assoc:test", "ti:test", "basic:test",
               "micropython:smoke"]

namespace :ui do
  # FmrbUI is pure Ruby over a few FmrbGfx calls, so the real mrblib file runs
  # under host Ruby against a recording stand-in. Cheapest tier there is: no
  # docker, no firmware, no device. Run it after touching fmrb-ui.rb.
  desc "FmrbUI widget tests (host Ruby, no docker)"
  task :test do
    sh "ruby test/fmrb_ui/run.rb"
  end
end

namespace :services do
  # The service host's rules -- the toml subset, the system/user merge, the
  # error budget, the sleep calculation -- are plain Ruby with no files,
  # messages or clock in them, so registry.rb runs here as it ships. Run it
  # after touching main/prebuild_scripts/default_app/services/.
  desc "Service host rule tests (host Ruby, no docker)"
  task :test do
    sh "ruby test/services/run.rb"
  end
end

namespace :assoc do
  # The file association table (doc/user_extension/assoc). Its resolution is
  # plain Ruby over Strings, so the gem file runs here with its two file reads
  # replaced. Run it after touching mrblib/fmrb-assoc.rb.
  desc "File association tests (host Ruby, no docker)"
  task :test do
    sh "ruby test/assoc/run.rb"
  end
end

namespace :test do
  # Opt-in sim/integration smoke: boot the headless Linux stack through the
  # root harness and assert it reaches the boot marker and paints a frame. It
  # is deliberately NOT part of `rake test` and NOT in CI -- it needs docker,
  # a framebuffer, and BOTH repos built for Linux (rake build:linux here and in
  # fmruby-graphics-audio). Run it by hand when a change could move the boot
  # path or the first screen. Deeper scripted E2E (input injection, per-app
  # screenshots) builds on the same tools/ harness; add cases here as they land.
  SIM_HARNESS = File.expand_path("../tools/dev_run_check.sh", ROOT_DIR)

  desc "Headless sim boot smoke (opt-in; needs docker + both repos built for Linux)"
  task :sim do
    abort "sim harness not found at #{SIM_HARNESS}" unless File.executable?(SIM_HARNESS)
    shot = File.join(ROOT_DIR, "vendor/sim_boot_smoke.png")
    rm_f shot
    # The harness waits for "main_loop started" and downs the stack on exit;
    # a non-zero status (boot never reached) fails the task via sh.
    sh SIM_HARNESS, shot
    unless File.exist?(shot) && File.size(shot) > 1024
      abort "sim booted but produced no frame at #{shot}"
    end
    puts "sim boot smoke OK: #{shot} (#{File.size(shot)} bytes)"
  end
end
