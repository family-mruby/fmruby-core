# rakelib/native.rake
# The native Linux program (doc/linux_native/): the firmware as one x86_64
# process with SDL2, the browser build's sibling. Independent of idf.py and
# of build:linux / build:esp32 -- it configures its own CMake (native/) on the
# host with the host's gcc, shares hosted/ with wasm/, and never writes to
# build/ or flash/.
#
#   rake native:core               build native/build/fmruby-native
#   rake native:core NATIVE_ENGINE=mruby
#                                  the all-mruby configuration, in
#                                  native/build-mruby
#   rake native:run                open the window (WSLg); ARGS="--scale 2"
#                                  or ARGS="--machine cardputer_zero_hdmi"
#   rake native:test               host tests of the backends' processing
#                                  halves (present core, key tables, evdev
#                                  decoding, fbdev formats, machine files)
#
# Headless use (screenshots, input, apps) goes through tools/fmrb_native.rb
# in the parent repository.

NATIVE_DIR = File.expand_path("native", ROOT_DIR)
NATIVE_ROOT_STAGE = File.join(NATIVE_DIR, "build", "root")
NATIVE_MRUBY_BUILD = "family-mruby-native"

# standard: Spinel kernel + Spinel editor, mruby desktop (the default every
# check runs first); mruby: kernel, desktop and editor all on mruby.
def native_engine
  engine = (ENV["NATIVE_ENGINE"] || "standard").strip
  abort "NATIVE_ENGINE must be standard or mruby (got #{engine})" unless %w[standard mruby].include?(engine)
  engine
end

def native_build_dir
  native_engine == "mruby" ? File.join(NATIVE_DIR, "build-mruby") : File.join(NATIVE_DIR, "build")
end

def native_engine_defines
  if native_engine == "mruby"
    "-DFMRB_KERNEL_ENGINE=mruby -DFMRB_APP_ENGINE_DESKTOP=mruby -DFMRB_APP_ENGINE_EDITOR=mruby"
  else
    "-DFMRB_KERNEL_ENGINE=spinel -DFMRB_APP_ENGINE_DESKTOP=mruby -DFMRB_APP_ENGINE_EDITOR=spinel"
  end
end

def native_binary
  File.join(native_build_dir, "fmruby-native")
end

namespace :native do
  # "^setup" is the ROOT setup task: the gem sources libmruby is built from
  # are the COPIES under components/picoruby-esp32/picoruby, and root setup
  # refreshes them from lib/add (wasm:mruby has the same dependency, for the
  # same reason).
  desc "Build libmruby for the native program (host gcc; run before native:core)"
  task :mruby => ["^setup", "hosted:m5gfx"] do
    picoruby = File.expand_path("components/picoruby-esp32/picoruby", ROOT_DIR)
    config = File.expand_path("lib/add/family_mruby_native.rb", ROOT_DIR)
    sh "cd #{picoruby} && MRUBY_CONFIG=#{config} rake"
  end

  desc "Regenerate stale mruby app/kernel bytecode (the native build cannot)"
  task :mrb do
    hosted_regen_mrb!("native:mrb", NATIVE_MRUBY_BUILD, "native:mruby")
  end

  # The first start's /etc and the machine definitions, staged outside
  # flash/: system_conf is generated from config/ (the rule for every target), and the repository's flash/etc
  # holds whatever the last simulation build put there. Tracked files of
  # flash/etc are carried over; the service list is its own
  # (config/services_native.toml: the simulation's, with timesync off --
  # the program keeps the PC's clock).
  desc "Stage the native program's first-start /etc and machine definitions into native/build/root"
  task :etc do
    etc = File.join(NATIVE_ROOT_STAGE, "etc")
    rm_rf etc
    mkdir_p etc
    `git -C #{ROOT_DIR} ls-files -z flash/etc`.split("\0").each do |f|
      dest = File.join(etc, f.sub(%r{\Aflash/etc/}, ""))
      mkdir_p File.dirname(dest)
      cp File.join(ROOT_DIR, f), dest
    end
    conf = File.join(ROOT_DIR, "config/system_conf_native.toml")
    cp conf, File.join(etc, "system_conf.toml")
    cp conf, File.join(etc, "system_conf.factory.toml")
    cp File.join(ROOT_DIR, "config/services_native.toml"), File.join(etc, "services.toml")
    cp File.join(ROOT_DIR, "config/associations.toml"), File.join(etc, "associations.toml")
    bad = Dir.glob(File.join(etc, "**/*")).grep(%r{wifi\.toml|secrets})
    abort "native:etc: refusing to stage #{bad.inspect}" unless bad.empty?
    puts "native:etc: #{etc}"
    # The machine definitions (--machine NAME), config/machines/ being the
    # source, like system_conf.
    machines = File.join(NATIVE_ROOT_STAGE, "machines")
    rm_rf machines
    mkdir_p machines
    Dir.glob(File.join(ROOT_DIR, "config/machines/*.toml")).sort.each { |f| cp f, machines }
    puts "native:etc: #{machines} (#{Dir.glob(File.join(machines, '*.toml')).size} machines)"
  end

  desc "Build the native program (NATIVE_ENGINE=standard|mruby; needs native:mruby once)"
  task :core => ["^setup", "hosted:m5gfx", :mrb, :etc] do
    gen = File.join(ROOT_DIR, "main/prebuild_scripts/spinel/gen")
    needed = %w[fft_kernel.c spinel_hello_kernel.c raycast_kernel.c]
    needed += %w[fmrb_kernel_combined.c editor_combined.c] if native_engine == "standard"
    missing = needed.reject { |f| File.exist?(File.join(gen, f)) }
    abort "native:core: #{missing.join(', ')} missing in #{gen}; run rake spinel:gen" unless missing.empty?
    lib_a = File.join(ROOT_DIR, "components/picoruby-esp32/picoruby/build",
                      NATIVE_MRUBY_BUILD, "lib/libmruby.a")
    Rake::Task["native:mruby"].invoke unless File.exist?(lib_a)
    dir = native_build_dir
    sh "cmake -S #{NATIVE_DIR} -B #{dir} -DCMAKE_BUILD_TYPE=RelWithDebInfo #{native_engine_defines}"
    sh "cmake --build #{dir} -j#{ENV['JOBS'] || 8} --target fmruby-native"
    puts "native:core: #{native_binary}"
  end

  desc "Run the native program in a window (ARGS=\"--scale 2 ...\")"
  task :run do
    bin = native_binary
    abort "#{bin} not found; run rake native:core first" unless File.exist?(bin)
    sh "#{bin} #{ENV['ARGS']}"
  end

  desc "Remove the native build directories (native/build, native/build-mruby)"
  task :clean do
    rm_rf File.join(NATIVE_DIR, "build")
    rm_rf File.join(NATIVE_DIR, "build-mruby")
  end
end

namespace :native do
  # The processing halves of the backends (doc/linux_native/ L2): the present
  # core on surfaces in memory, the key tables, evdev decoding of recorded
  # streams through a pipe, the fbdev format reader and the machine files.
  # WSL2 has none of the devices, so what opens them is only built (by
  # native:core); this is what can be checked without them. Host cc only.
  NATIVE_TEST_DIR = "test/native"
  NATIVE_TEST_BUILD = "#{NATIVE_TEST_DIR}/build"

  desc "Host tests of the native backends' processing halves (cc, no device)"
  task :test do
    cc = ENV["CC"] || "cc"
    abort "#{cc} not found (install a C compiler or set CC)" unless system("which #{cc} > /dev/null 2>&1")
    mkdir_p NATIVE_TEST_BUILD
    toml = "components/toml_parser/tomlc99"
    # tomlc99 is a submodule with its own style; built apart, warnings off.
    sh "#{cc} -std=c11 -O1 -g -w -I #{toml} -c #{toml}/toml.c -o #{NATIVE_TEST_BUILD}/toml.o"
    incs = %w[native/present native/input native/machine native/output main/drivers/usb].push(toml)
    srcs = %w[native/present/fmrb_present.c native/input/linux_keys.c native/input/keymap_cardputer_zero.c
              native/input/evdev_decode.c native/output/fbdev_info.c native/machine/native_machine.c
              main/drivers/usb/fmrb_keymap.c].push("#{NATIVE_TEST_DIR}/main.c")
    sh "#{cc} -std=gnu11 -O1 -g -Wall -Wextra -Werror #{incs.map { |i| "-I #{i}" }.join(' ')} " \
       "#{srcs.join(' ')} #{NATIVE_TEST_BUILD}/toml.o -lm -o #{NATIVE_TEST_BUILD}/native_test"
    sh "#{NATIVE_TEST_BUILD}/native_test #{ROOT_DIR}"
  end
end
