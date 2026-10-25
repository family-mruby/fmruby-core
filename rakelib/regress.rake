# rakelib/regress.rake
# One command for the cheap regression checks (doc/regression/usage.md):
#
#   rake regress           the host suites of `rake test` (each one's output to
#                          tmp/regress/host/<suite>.log) and the draw-record
#                          comparison (tools/regress/run.rb); a short summary,
#                          the whole diff in tmp/regress/diff.txt
#   rake regress:draw      the draw-record comparison alone (CI runs it after
#                          rake test). ONLY=<text> narrows it to matching runs
#   rake regress:update    write the records as the new baseline, listing what
#                          changed. Only after a person has looked at the diff
#                          and the change is the one intended
#
# No docker, no sim, no device.

REGRESS_RUN = File.join(ROOT_DIR, "tools", "regress", "run.rb")

def regress_draw_args
  ENV["ONLY"] ? ["--only", ENV["ONLY"]] : []
end

desc "Host tests + draw-record comparison, short summary (full diff in tmp/regress/diff.txt)"
task :regress do
  require "open3"
  log_dir = File.join(ROOT_DIR, "tmp", "regress", "host")
  mkdir_p log_dir, verbose: false
  suites = Rake::Task[:test].prerequisites
  failed = []
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  suites.each do |s|
    log = File.join(log_dir, "#{s.tr(':', '_')}.log")
    out, st = Open3.capture2e(ENV["RAKE"] || "rake", "-s", s, chdir: ROOT_DIR)
    File.write(log, out)
    failed << [s, log, out] unless st.success?
  end
  took = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
  failed.each do |s, log, out|
    puts "FAIL host #{s} (#{log.sub(ROOT_DIR + '/', '')})"
    out.lines.last(4).each { |l| puts "     #{l.rstrip}" }
  end
  puts format("host: %d suites, %d passed, %d failed (%.1f s)", suites.size, suites.size - failed.size, failed.size, took)
  draw_ok = system(Gem.ruby, REGRESS_RUN, *regress_draw_args)
  abort "regress: failed" unless failed.empty? && draw_ok
end

namespace :regress do
  desc "Draw-record comparison only (tools/regress/run.rb; ONLY=<text> narrows it)"
  task :draw do
    ok = system(Gem.ruby, REGRESS_RUN, *regress_draw_args)
    abort "regress:draw: failed (full diff: tmp/regress/diff.txt)" unless ok
  end

  desc "Write the draw records as the new baseline (only for a change a person has checked)"
  task :update do
    ok = system(Gem.ruby, REGRESS_RUN, "--update", *regress_draw_args)
    abort "regress:update: some runs failed" unless ok
  end
end
