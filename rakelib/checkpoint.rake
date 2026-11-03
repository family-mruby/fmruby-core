# rakelib/checkpoint.rake
# mruby-checkpoint (doc/checkpoint): fetch the pinned family-mruby/
# mruby-checkpoint into vendor/. Part of the Rakefile split: shared constants
# and helpers live in the top-level Rakefile, which Rake loads before every
# file in rakelib/.

namespace :checkpoint do
  desc "Fetch the pinned mruby-checkpoint into vendor/mruby-checkpoint (https)"
  task :setup do
    # A checkout given by MRUBY_CHECKPOINT_DIR is used as it is and not fetched.
    if MRUBY_CHECKPOINT_DIR == MRUBY_CHECKPOINT_VENDOR_DIR
      fetch_pinned_checkout("mruby-checkpoint", mruby_checkpoint_pin, MRUBY_CHECKPOINT_VENDOR_DIR)
    end
  end
end
