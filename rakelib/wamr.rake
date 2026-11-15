# rakelib/wamr.rake
# WAMR (doc/wasm_apps): fetch the pinned wasm-micro-runtime into vendor/wamr.
# components/fmrb_wamr builds it into the P4 and Linux firmware. Part of the
# Rakefile split: shared constants and helpers live in the top-level Rakefile.

namespace :wamr do
  desc "Fetch the pinned WAMR into vendor/wamr (https)"
  task :setup do
    fetch_pinned_checkout("WAMR", wamr_pin, WAMR_VENDOR_DIR)
  end
end
