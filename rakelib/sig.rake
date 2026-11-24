# rakelib/sig.rake
# What sig/*.rbs is missing (doc/sig_coverage): the apps' calls, the
# implementation's methods and the doc comments, counted by
# tools/sig_coverage/coverage.rb (host ruby with the rbs gem, Prism).
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.
#
# A report, not a gate (doc/sig_coverage decision 3): it is not in
# `rake regress`. The summary goes to stdout, the details to
# tmp/sig_coverage/report.md, and the difference from
# tools/sig_coverage/baseline.json is printed every time.

namespace :sig do
  desc "Count what sig/ is missing: apps' calls, implemented methods, doc gaps (report only)"
  task :coverage do
    sh "#{RbConfig.ruby} tools/sig_coverage/coverage.rb"
  end

  namespace :coverage do
    desc "Count as sig:coverage and record the result as tools/sig_coverage/baseline.json"
    task :update do
      sh "#{RbConfig.ruby} tools/sig_coverage/coverage.rb --update"
    end
  end

  # The tool against its own small tree (tools/sig_coverage/test/fixture).
  desc "sig coverage tool tests (host Ruby, no docker)"
  task :test do
    sh "#{RbConfig.ruby} tools/sig_coverage/test/run.rb"
  end
end
