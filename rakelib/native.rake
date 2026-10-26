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

  # The first start's /etc, staged outside flash/: system_conf is generated
  # from config/ (the rule for every target), and the repository's flash/etc
  # holds whatever the last simulation build put there. Tracked files of
  # flash/etc are carried over; the service list is the browser's, the
  # services that need no network (net and timesync open blocking sockets,
  # which would stop a cooperative scheduler).
  desc "Stage the native program's first-start /etc into native/build/root/etc"
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
    cp File.join(ROOT_DIR, "config/services_wasm.toml"), File.join(etc, "services.toml")
    cp File.join(ROOT_DIR, "config/associations.toml"), File.join(etc, "associations.toml")
    bad = Dir.glob(File.join(etc, "**/*")).grep(%r{wifi\.toml|secrets})
    abort "native:etc: refusing to stage #{bad.inspect}" unless bad.empty?
    puts "native:etc: #{etc}"
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
