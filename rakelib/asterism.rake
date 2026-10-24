# rakelib/asterism.rake
# Asterism (doc/ruby_asterism): fetch the pinned ruby-asterism repositories,
# and run the message type tests of the asterism checkout. Part of the
# Rakefile split: shared constants and helpers live in the top-level Rakefile,
# which Rake loads before every file in rakelib/.

namespace :asterism do
  desc "Fetch the pinned asterism and picoruby-asterism-zenoh into vendor/ (ssh)"
  task :setup do
    # A checkout given by ASTERISM_DIR / PICORUBY_ASTERISM_ZENOH_DIR is used
    # as it is and not fetched.
    if ASTERISM_DIR == ASTERISM_VENDOR_DIR
      fetch_pinned_checkout("asterism", asterism_pin, ASTERISM_VENDOR_DIR)
    end
    if PICORUBY_ASTERISM_ZENOH_DIR == PICORUBY_ASTERISM_ZENOH_VENDOR_DIR
      fetch_pinned_checkout("picoruby-asterism-zenoh", picoruby_asterism_zenoh_pin,
                            PICORUBY_ASTERISM_ZENOH_VENDOR_DIR)
    end
  end

  # The type tests (generator, type hashes, CDR, bundled types) belong to the
  # asterism repository now; this runs them on the checkout the firmware is
  # built from. CRuby only, no docker. Not part of `rake test`: CI cannot
  # fetch the private repository.
  desc "Run the asterism checkout's message type tests (CRuby, no docker)"
  task :test do
    sh "ruby #{File.join(asterism_dir!, "test/msgs/run.rb")}"
  end

  # FmrbAsterism.settings (CF1): where a machine's router, node ID, name and
  # credentials come from, and the move of the files before CF1. The gem
  # file runs under CRuby with its paths in a temporary directory, and the
  # credentials are read by the checkout's own Asterism::Credentials.
  desc "Run the FmrbAsterism settings tests (CRuby, no docker)"
  task :config_test do
    creds = File.join(asterism_dir!, "mrblib/credentials.rb")
    sh({ "ASTERISM_CREDENTIALS_RB" => creds }, "ruby test/asterism/run.rb")
  end

  # FmrbAsterism::Link (doc/app_refactor, R1): waiting for WiFi and the
  # screen, connecting, retrying, a lost connection, the application's
  # setup after every connect. The gem file runs under CRuby against a fake
  # Asterism; it needs no checkout, so it is part of `rake test`.
  desc "Run the FmrbAsterism::Link tests (CRuby, no docker)"
  task :link_test do
    sh "ruby test/asterism/link_run.rb"
  end

  # FmrbAsterism::TableClient (doc/app_refactor, R4): the list of a game
  # host's tables, join / leave, the view, the host gone and back, on top of
  # Link. Against a fake Asterism and a fake game host; part of `rake test`.
  desc "Run the FmrbAsterism::TableClient tests (CRuby, no docker)"
  task :table_test do
    sh "ruby test/asterism/table_run.rb"
  end
end
