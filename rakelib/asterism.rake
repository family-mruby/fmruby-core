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
end
