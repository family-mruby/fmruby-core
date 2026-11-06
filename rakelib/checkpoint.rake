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

# The checkpoint table of a configuration (doc/checkpoint/flash_table.md,
# Ff): the names, snapshots and method list of a movable app's baseline, as C
# source the firmware links as constant data. The native program, the Linux
# simulation and the browser build that carries the gem make theirs inside
# their own build (tools/ckpt_table/ckpt_table.cmake), again whenever libmruby
# or the gem ports change. The devices (P4, S3) carry the gem from F6; their
# tables are made here, on the host, from a host build of the same gems
# (lib/add/family_mruby_ckpt_table.rb) and the gem ports compiled as the Linux
# targets compile them (tools/ckpt_table/CMakeLists.txt).
namespace :checkpoint do
  CKPT_TABLE_TMP = File.join(ROOT_DIR, "tmp", "ckpt_table")

  desc "Make the checkpoint table of a device configuration on the host (FMRB_HW_TARGET=TAB5|NARYAv4|NARYAv3|ATOMS3R|STICKS3; tmp/ckpt_table/<board>/)"
  task :table => ["^setup", "hosted:m5gfx"] do
    board = HW_TARGET.empty? ? "NARYAv3" : HW_TARGET
    of = ESP_CHIP == "esp32p4" ? "esp32p4" : "esp32"
    picoruby = File.join(ROOT_DIR, "components/picoruby-esp32/picoruby")
    config = File.join(ROOT_DIR, "lib/add/family_mruby_ckpt_table.rb")
    # the board's build defaults are in ENV already (the Rakefile); the
    # device's config file reads them as it does in the container
    env = { "MRUBY_CONFIG" => config, "FMRB_CKPT_TABLE_OF" => of, "FMRB_HW_TARGET" => board }
    sh env, "rake", chdir: picoruby
    dir = File.join(CKPT_TABLE_TMP, board)
    out = File.join(dir, "fmrb_checkpoint_table.c")
    sh "cmake", "-S", File.join(ROOT_DIR, "tools/ckpt_table"), "-B", File.join(dir, "build"),
       "-DCMAKE_BUILD_TYPE=Release", "-DFMRB_CKPT_OF=#{of}", "-DIDF_TARGET=#{ESP_CHIP}",
       "-DFMRB_HW_TARGET=#{board}", "-DFMRB_CKPT_OUT=#{out}"
    sh "cmake", "--build", File.join(dir, "build"), "-j#{ENV['JOBS'] || 8}"
    head = File.foreach(out).first(3).join
    puts "checkpoint:table: #{out}"
    puts head[/\d+ names, \d+ snapshots, \d+ mrblib ireps/].to_s
    # what a VM's baseline costs with it, on the host (64-bit pointers)
    measure = File.join(dir, "measure.txt")
    puts File.read(measure) if File.exist?(measure)
  end
end
