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
#                                  decoding, fbdev formats, machine files,
#                                  the file namespace, the pty programs and
#                                  the Shell's reading of their output, the
#                                  compositor's routing and frame hand-over)
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
    incs = %w[native/present native/input native/machine native/output native/etc native/sys main/kernel
              main/drivers/usb components/fmrb_common/include].push(toml)
    srcs = %w[native/present/fmrb_present.c native/input/linux_keys.c native/input/keymap_cardputer_zero.c
              native/input/evdev_decode.c native/output/fbdev_info.c native/machine/native_machine.c
              native/etc/etc_update.c native/sys/sys_linux_read.c
              main/drivers/usb/fmrb_keymap.c].push("#{NATIVE_TEST_DIR}/main.c")
    sh "#{cc} -std=gnu11 -O1 -g -Wall -Wextra -Werror #{incs.map { |i| "-I #{i}" }.join(' ')} " \
       "#{srcs.join(' ')} #{NATIVE_TEST_BUILD}/toml.o -lm -o #{NATIVE_TEST_BUILD}/native_test"
    sh "#{NATIVE_TEST_BUILD}/native_test #{ROOT_DIR}"

    # The file namespace's Linux places (L7): the POSIX file HAL itself, with
    # test/native/stub standing in for the RTOS, the pools and the log.
    fincs = %W[#{NATIVE_TEST_DIR}/stub components/fmrb_hal components/fmrb_hal/platform/posix
               components/fmrb_common/include]
    fsrcs = %w[components/fmrb_hal/platform/posix/fmrb_hal_file_posix.c
               components/fmrb_hal/platform/posix/fmrb_tmpfs_posix.c].push("#{NATIVE_TEST_DIR}/file_hal.c")
    sh "#{cc} -std=gnu11 -O1 -g -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_LINUX " \
       "#{fincs.map { |i| "-I #{i}" }.join(' ')} #{fsrcs.join(' ')} -o #{NATIVE_TEST_BUILD}/native_file_test"
    sh "#{NATIVE_TEST_BUILD}/native_file_test"

    # Linux programs under pseudo terminals (L10): native/sys/linux_pty.c is
    # plain POSIX and runs as it is.
    pincs = %w[native/sys components/fmrb_common/include]
    sh "#{cc} -std=gnu11 -O1 -g -Wall -Wextra -Werror #{pincs.map { |i| "-I #{i}" }.join(' ')} " \
       "native/sys/linux_pty.c #{NATIVE_TEST_DIR}/pty.c -lpthread -o #{NATIVE_TEST_BUILD}/native_pty_test"
    sh "#{NATIVE_TEST_BUILD}/native_pty_test"
    # ...and the Shell's reading of their output (lines and SGR colours).
    sh "ruby #{NATIVE_TEST_DIR}/shell_term_test.rb"

    # The compositor's hand-overs (L8) without wlroots: where things lie and
    # who gets the input (native/wlr/comp_route.c), and the frame handed from
    # one thread to another with an eventfd (native/backend/native_frame.c).
    cincs = %w[native/wlr native/present native/backend]
    csrcs = %w[native/wlr/comp_route.c native/present/fmrb_present.c native/backend/native_frame.c]
              .push("#{NATIVE_TEST_DIR}/comp.c")
    sh "#{cc} -std=gnu11 -O1 -g -Wall -Wextra -Werror #{cincs.map { |i| "-I #{i}" }.join(' ')} " \
       "#{csrcs.join(' ')} -lpthread -o #{NATIVE_TEST_BUILD}/native_comp_test"
    sh "#{NATIVE_TEST_BUILD}/native_comp_test"
  end
end

# ---------------------------------------------------------------- packages
#
# The .deb packages (doc/linux_native/report/l3a.md). Each is built inside
# an image of native/docker/, pinned: arm64 in Debian trixie (Dockerfile;
# what the Cardputer Zero's Raspberry Pi OS trixie has), amd64 in Ubuntu
# 26.04 (Dockerfile.ubuntu, L8: Kubuntu 26.04 PCs, and wlroots 0.19 for the
# compositor). The developer's own build above stays the host's (Ubuntu
# 22.04, no wlroots) and is untouched.
#
#   rake native:image ARCH=..      build the architecture's image (once; again
#                                  when its Dockerfile changes). Local only,
#                                  never pushed
#   rake native:cross ARCH=arm64   libmruby and the program for one
#                                  architecture (arm64 cross, amd64 native)
#   rake native:root               the distribution tree: git-tracked flash/
#                                  only, plus the generated etc, machines,
#                                  help and Asterism messages
#   rake native:deb ARCH=arm64     the package, then native:scan on it
#   rake native:scan ARCH=arm64    no untracked file, no credential
#
# The compositor (L8) is built and run in the amd64 image too, since only
# it has wlroots 0.19:
#
#   rake native:wl                 a development build with the compositor,
#                                  in native/build-wl (the image's own
#                                  libmruby in native/build-wl/mruby)
#   rake native:wl_run             run it in a container, nested in WSLg's
#                                  Wayland (ARGS="--set compositor.autostart=foot"
#                                  by default); the window closes with it
#   rake native:wl_check           the compositor's checks in a container,
#                                  headless: grim against the picture, keys
#                                  to foot and to Family mruby, timings
#                                  (test/native/wl/wl_check.rb)
#
# Everything lands in native/build-deb/ (git-ignored). The repository is
# mounted into the container at /src and the build runs as the calling user.

require "shellwords"

NATIVE_DEB_DIR = File.join(NATIVE_DIR, "build-deb")
NATIVE_DOCKERFILES = {
  "arm64" => File.join(NATIVE_DIR, "docker", "Dockerfile"),
  "amd64" => File.join(NATIVE_DIR, "docker", "Dockerfile.ubuntu"),
}.freeze

# The layout of each package. arm64 is the Cardputer Zero's: M5Stack's
# APPLaunch launcher looks in /usr/share/APPLaunch for programs, entries
# and icons. amd64 is a PC's ordinary /usr/bin, with the login screen's
# session entry (L7). The distribution tree is in the same place for both.
NATIVE_DEB_LAYOUT = {
  "arm64" => {
    bin_dir: "usr/share/APPLaunch/bin",
    data_rel: "../../family-mruby/root",
    gnu: "aarch64-linux-gnu",
    # suffix => [machine, the entry's name]. The device's own 320x170
    # screen is the main entry, "Family mruby" (L5a); the HDMI output is
    # the one that says so.
    launchers: {
      "lcd" => ["cardputer_zero", "Family mruby"],
      "hdmi" => ["cardputer_zero_hdmi", "Family mruby (HDMI)"],
    },
  },
  "amd64" => {
    bin_dir: "usr/bin",
    data_rel: "../share/family-mruby/root",
    gnu: "x86_64-linux-gnu",
    launchers: {},
    # The login screen's entry (L7): a Wayland session that runs cage with
    # the program inside it (native/package/family-mruby-session, machine
    # linux_pc). cage is recommended, not required: without it the entry is
    # still listed and the session says what is missing.
    session: true,
    recommends: "cage",
  },
}.freeze
NATIVE_DEB_ROOT = "usr/share/family-mruby/root"
NATIVE_DEB_SESSIONS = "usr/share/wayland-sessions"

def native_deb_arch
  arch = (ENV["ARCH"] || "arm64").strip
  abort "ARCH must be one of #{NATIVE_DEB_LAYOUT.keys.join(', ')} (got #{arch})" unless NATIVE_DEB_LAYOUT.key?(arch)
  arch
end

# The image's tag: the base's tag and the Dockerfile's own hash, so a
# changed Dockerfile is a new image rather than a stale one.
def native_image(arch = native_deb_arch)
  require "digest"
  file = NATIVE_DOCKERFILES.fetch(arch)
  from = File.read(file)[/^FROM\s+(?:debian|ubuntu):(\S+)/, 1] or abort "native:image: no FROM debian:/ubuntu: in #{file}"
  "fmrb-native-build:#{from}-#{Digest::SHA256.file(file).hexdigest[0, 8]}"
end

# A script in the build image, as the calling user, with the repository at /src.
def native_docker(script, env = {}, arch: native_deb_arch)
  envs = env.map { |k, v| "-e #{k}=#{Shellwords.escape(v)}" }.join(" ")
  sh "docker run --rm -u #{Process.uid}:#{Process.gid} -v #{ROOT_DIR}:/src -w /src #{envs} " \
     "#{native_image(arch)} bash -euc #{Shellwords.escape(script)}"
end

def native_image_build(arch)
  img = native_image(arch)
  if system("docker image inspect #{img} > /dev/null 2>&1")
    puts "native:image: #{img} (present)"
  else
    file = NATIVE_DOCKERFILES.fetch(arch)
    sh "docker build -t #{img} -f #{file} #{File.dirname(file)}"
    puts "native:image: #{img}"
  end
end

def native_deb_version
  d = `git -C #{ROOT_DIR} describe --tags --long --match '[0-9]*' 2>/dev/null`.strip
  m = /\A(\d+(?:\.\d+)*)-(\d+)-g([0-9a-f]+)\z/.match(d) or abort "native:deb: cannot make a version from git describe (#{d.inspect})"
  m[2] == "0" ? m[1] : "#{m[1]}+git#{m[2]}.#{m[3]}"
end

namespace :native do
  desc "Build the package build image (ARCH=arm64: Debian trixie, amd64: Ubuntu 26.04; local only)"
  task :image do
    native_image_build(native_deb_arch)
  end

  desc "Build libmruby and the program for a package (ARCH=arm64|amd64, in the build image)"
  task :cross => ["^setup", "hosted:m5gfx", :mrb, :image] do
    arch = native_deb_arch
    lay = NATIVE_DEB_LAYOUT[arch]
    gen = File.join(ROOT_DIR, "main/prebuild_scripts/spinel/gen")
    needed = %w[fft_kernel.c spinel_hello_kernel.c raycast_kernel.c fmrb_kernel_combined.c editor_combined.c]
    missing = needed.reject { |f| File.exist?(File.join(gen, f)) }
    abort "native:cross: #{missing.join(', ')} missing in #{gen}; run rake spinel:gen" unless missing.empty?
    dir = File.join(NATIVE_DEB_DIR, arch)
    mkdir_p dir
    rel = dir.sub("#{ROOT_DIR}/", "")
    # libmruby: the same configuration as the developer's, in a build tree
    # of its own (the host tools in it are the image's, not the host's).
    native_docker("cd components/picoruby-esp32/picoruby && rake -j#{ENV['JOBS'] || 8}",
                  { "FMRB_NATIVE_ARCH" => arch,
                    "MRUBY_BUILD_DIR" => "/src/#{rel}/mruby",
                    "MRUBY_CONFIG" => "/src/lib/add/family_mruby_native.rb" })
    toolchain = arch == "arm64" ? "-DCMAKE_TOOLCHAIN_FILE=/src/native/docker/aarch64-linux-gnu.cmake" : ""
    launchers = lay[:launchers].map { |suffix, (machine, _)| "#{suffix}=#{machine}" }.join(";")
    targets = ["fmruby-native"] + lay[:launchers].keys.map { |s| "family-mruby-#{s}" }
    native_docker(<<~SH)
      cmake -S native -B #{rel}/cmake #{toolchain} -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        #{native_engine_defines} -DNATIVE_PACKAGE=ON \
        -DNATIVE_PACKAGE_DATA_REL=#{lay[:data_rel]} '-DNATIVE_LAUNCHERS=#{launchers}' \
        -DFMRB_HOSTED_MRUBY_DIR=/src/#{rel}/mruby/family-mruby-native
      cmake --build #{rel}/cmake -j#{ENV['JOBS'] || 8} --target #{targets.join(' ')}
      file #{rel}/cmake/fmruby-native
    SH
  end

  # The release system_conf differs from the development one in these keys
  # only (doc/linux_native/report/l3c.md); comments may differ.
  NATIVE_RELEASE_CONF = "config/system_conf_native_release.toml"
  NATIVE_RELEASE_CONF_KEYS = %w[debug_mode zenoh_router debugd].freeze

  def native_check_release_conf!(dev, release)
    settings = lambda do |path|
      File.readlines(path).map(&:strip).reject { |l| l.empty? || l.start_with?("#") }
          .map { |l| l.sub(/\s+#.*\z/, "") }
          .reject { |l| NATIVE_RELEASE_CONF_KEYS.include?(l[/\A([A-Za-z0-9_]+)\s*=/, 1]) }
    end
    a = settings.call(dev)
    b = settings.call(release)
    return if a == b
    extra = (b - a).first(3)
    missing = (a - b).first(3)
    abort "native:root: #{NATIVE_RELEASE_CONF} drifted from system_conf_native.toml beyond " \
          "#{NATIVE_RELEASE_CONF_KEYS.join(', ')} (only in release: #{extra.inspect}, only in development: #{missing.inspect})"
  end

  desc "Stage the package's distribution tree (git-tracked flash/ + generated etc, machines, help)"
  task :root => [:etc, "ti:help"] do
    root = File.join(NATIVE_DEB_DIR, "root")
    rm_rf root
    mkdir_p root
    files = `git -C #{ROOT_DIR} ls-files -z flash`.split("\0")
    abort "native:root: git ls-files returned nothing" if files.empty?
    # Same rights rule as the browser bundle (wasm:webflash): only our own
    # tunes ship. And nothing under the places the native layout gives to
    # the user or the cache (/etc /home /app/usr /var /cache /data /tmp):
    # the program never reads them from the distribution tree.
    user_tops = %r{\Aflash/(etc|home|app/usr|var|cache|data|tmp)/}
    files = files.reject { |f| f.end_with?(".nsf") && !f.start_with?("flash/usr/share/sounds/nsf/") }
    files = files.reject { |f| f =~ user_tops }
    files.each do |f|
      dest = File.join(root, f.sub(%r{\Aflash/}, ""))
      mkdir_p File.dirname(dest)
      cp File.join(ROOT_DIR, f), dest
    end
    msgs = File.join(ROOT_DIR, ASTERISM_MSGS_DIR)
    abort "native:root: #{ASTERISM_MSGS_DIR} is missing (rake setup makes it)" unless Dir.exist?(msgs)
    mkdir_p File.join(root, "usr/share/asterism")
    cp_r msgs, File.join(root, "usr/share/asterism/msgs")
    help_src = File.join(ROOT_DIR, "flash/help")
    Dir.glob(File.join(help_src, "**/*")).select { |f| File.file?(f) }.each do |f|
      dest = File.join(root, "help", f.sub(help_src + "/", ""))
      mkdir_p File.dirname(dest)
      cp f, dest
    end
    # The first start's /etc (seed of the user's, and what later releases
    # add to it) and the machine definitions, as native:etc staged them.
    cp_r File.join(NATIVE_ROOT_STAGE, "etc"), File.join(root, "etc")
    cp_r File.join(NATIVE_ROOT_STAGE, "machines"), File.join(root, "machines")
    # The package's system_conf is the release one (L3c): the development
    # file's settings but for NATIVE_RELEASE_CONF_KEYS.
    release = File.join(ROOT_DIR, NATIVE_RELEASE_CONF)
    native_check_release_conf!(File.join(ROOT_DIR, "config/system_conf_native.toml"), release)
    %w[system_conf.toml system_conf.factory.toml].each { |n| cp release, File.join(root, "etc", n) }
    puts "native:root: #{files.length} tracked files + etc, machines, help, Asterism messages -> #{root}"
  end

  desc "Build the .deb (ARCH=arm64|amd64) into native/build-deb/, then scan it"
  task :deb => [:cross, :root] do
    arch = native_deb_arch
    lay = NATIVE_DEB_LAYOUT[arch]
    dir = File.join(NATIVE_DEB_DIR, arch)
    rel = dir.sub("#{ROOT_DIR}/", "")
    pkg = File.join(dir, "pkg")
    rm_rf pkg
    bin = File.join(pkg, lay[:bin_dir])
    mkdir_p bin
    cp File.join(dir, "cmake", "fmruby-native"), File.join(bin, "family-mruby")
    lay[:launchers].each_key { |s| cp File.join(dir, "cmake", "family-mruby-#{s}"), bin }
    if lay[:session]
      # The session's starter (a shell script, not built) and its entry.
      cp File.join(NATIVE_DIR, "package", "family-mruby-session"), bin
      sessions = File.join(pkg, NATIVE_DEB_SESSIONS)
      mkdir_p sessions
      cp File.join(NATIVE_DIR, "package", "family-mruby.desktop"), sessions
    end
    Dir.glob(File.join(bin, "*")).each { |f| chmod 0o755, f }
    mkdir_p File.dirname(File.join(pkg, NATIVE_DEB_ROOT))
    cp_r File.join(NATIVE_DEB_DIR, "root"), File.join(pkg, NATIVE_DEB_ROOT)
    unless lay[:launchers].empty?
      # APPLaunch: an entry per launcher (Exec is a path to an executable,
      # no arguments, no shell), and the icon (a 1:1 PNG of our own: the
      # boot logo).
      apps = File.join(pkg, "usr/share/APPLaunch/applications")
      icons = File.join(pkg, "usr/share/APPLaunch/share/images")
      mkdir_p apps
      mkdir_p icons
      cp File.join(ROOT_DIR, "flash/boot/boot.png"), File.join(icons, "family-mruby.png")
      lay[:launchers].each do |suffix, (machine, title)|
        File.write(File.join(apps, "family-mruby-#{suffix}.desktop"), <<~DESKTOP)
          [Desktop Entry]
          Type=Application
          Name=#{title}
          Comment=Family mruby on the #{machine} machine definition
          Exec=/#{lay[:bin_dir]}/family-mruby-#{suffix}
          Icon=/usr/share/APPLaunch/share/images/family-mruby.png
          Terminal=false
          Categories=Development;Education;
        DESKTOP
      end
    end
    version = native_deb_version
    deb = File.join(NATIVE_DEB_DIR, "family-mruby_#{version}_#{arch}.deb")
    maintainer = ENV["FMRB_DEB_MAINTAINER"] || "Family mruby developers <noreply@family-mruby.invalid>"
    # Strip in the image (the cross binutils), work out Depends from the
    # libraries the binaries need (dpkg-shlibdeps, against the image's
    # trixie packages), write the control file and build.
    bins = Dir.glob(File.join(bin, "*")).reject { |f| File.read(f, 2) == "#!" }.map { |f| f.sub("#{ROOT_DIR}/", "") }
    native_docker(<<~SH)
      #{lay[:gnu]}-strip --strip-unneeded #{bins.join(' ')}
      work=$(mktemp -d)
      mkdir -p $work/debian
      printf 'Source: family-mruby\\n\\nPackage: family-mruby\\nArchitecture: #{arch}\\n' > $work/debian/control
      (cd $work && dpkg-shlibdeps -O --ignore-missing-info -l/usr/lib/#{lay[:gnu]} #{bins.map { |b| "/src/#{b}" }.join(' ')}) \
        > #{rel}/shlibs.txt
      rm -rf $work
    SH
    depends = File.read(File.join(dir, "shlibs.txt"))[/shlibs:Depends=(.*)$/, 1] or abort "native:deb: dpkg-shlibdeps gave no Depends"
    # libsystemd0 is opened at run time (dlopen, native/sys/platform_sys_linux.c:
    # logind's power entries), so dpkg-shlibdeps cannot see it.
    depends = "#{depends.strip}, libsystemd0"
    size_kb = `du -sk --exclude=DEBIAN #{pkg}`.to_i
    mkdir_p File.join(pkg, "DEBIAN")
    File.write(File.join(pkg, "DEBIAN", "control"), <<~CONTROL)
      Package: family-mruby
      Version: #{version}
      Architecture: #{arch}
      Maintainer: #{maintainer}
      Installed-Size: #{size_kb}
      Depends: #{depends.strip}
      #{lay[:recommends] ? "Recommends: #{lay[:recommends]}\n" : ''}Section: misc
      Priority: optional
      Homepage: https://github.com/family-mruby
      Description: Family mruby, a Ruby computer, as a Linux program
       The Family mruby firmware (the Modern desktop, its apps and the Ruby
       engines) running as one Linux process. The distribution tree is in
       /usr/share/family-mruby/root; each user's files go to
       $XDG_DATA_HOME/family-mruby and $XDG_CACHE_HOME/family-mruby.
    CONTROL
    rm_f deb
    native_docker("dpkg-deb --root-owner-group -Zxz --build #{rel}/pkg #{deb.sub("#{ROOT_DIR}/", '')}")
    puts "native:deb: #{deb} (#{(File.size(deb) / 1024.0 / 1024).round(1)} MB, installed #{(size_kb / 1024.0).round(1)} MB)"
    puts "  Depends: #{depends.strip}"
    Rake::Task["native:scan"].invoke
  end

  # What a package may contain is decided in native:root (git-tracked flash/
  # only); this checks the RESULT, the staged package tree, the way wasm:scan
  # checks what the browser bundle packed.
  desc "Check a staged package (ARCH=...): no untracked file, no credential"
  task :scan do
    arch = native_deb_arch
    pkg = File.join(NATIVE_DEB_DIR, arch, "pkg")
    abort "#{pkg} not found; run rake native:deb ARCH=#{arch} first" unless Dir.exist?(pkg)
    root = File.join(pkg, NATIVE_DEB_ROOT)
    problems = []
    tracked = `git -C #{ROOT_DIR} ls-files -z flash`.split("\0").to_set
    generated = Dir.glob(File.join(NATIVE_ROOT_STAGE, "{etc,machines}", "**", "*"))
                   .select { |f| File.file?(f) }.map { |f| f.sub("#{NATIVE_ROOT_STAGE}/", "") }.to_set
    files = Dir.glob(File.join(pkg, "**", "*"), File::FNM_DOTMATCH).select { |f| File.file?(f) }
    files.each do |f|
      r = f.sub("#{pkg}/", "")
      next if r.start_with?("DEBIAN/")
      if r.start_with?("#{NATIVE_DEB_ROOT}/")
        t = r.sub("#{NATIVE_DEB_ROOT}/", "")
        next if tracked.include?("flash/#{t}")
        next if generated.include?(t)
        # The editor's help (ti:help from sig/) and Asterism's message types
        # (rake setup from the pinned asterism checkout).
        next if t.start_with?("help/") || t.start_with?("usr/share/asterism/msgs/")
        problems << "neither tracked in flash/ nor a known generated file: #{r}"
      else
        layout = NATIVE_DEB_LAYOUT[arch]
        ok = r.start_with?("#{layout[:bin_dir]}/") || r.start_with?("usr/share/APPLaunch/") ||
             (layout[:session] && r == "#{NATIVE_DEB_SESSIONS}/family-mruby.desktop")
        problems << "outside the package layout: #{r}" unless ok
      end
      base = File.basename(r)
      problems << "credential file by name: #{r}" if base =~ /\A(wifi\.toml|secrets\.toml)/
      problems << "NSF tune from outside usr/share/sounds/nsf (rights): #{r}" if r.end_with?(".nsf") && !r.include?("/usr/share/sounds/nsf/")
    end
    problems << "the distribution tree ships a /home" if Dir.exist?(File.join(root, "home"))
    # The package itself holds exactly the staged files (nothing between the
    # staging and dpkg-deb added any).
    deb = Dir[File.join(NATIVE_DEB_DIR, "family-mruby_*_#{arch}.deb")].max_by { |f| File.mtime(f) }
    if deb
      in_deb = `dpkg-deb -c #{Shellwords.escape(deb)}`.lines.reject { |l| l.start_with?("d") }
                                                       .map { |l| l.split(" ", 6)[5].strip.sub(%r{\A\./}, "") }.sort
      staged = files.map { |f| f.sub("#{pkg}/", "") }.reject { |r| r.start_with?("DEBIAN/") }.sort
      (in_deb - staged).each { |r| problems << "in #{File.basename(deb)} but not staged: #{r}" }
      (staged - in_deb).each { |r| problems << "staged but not in #{File.basename(deb)}: #{r}" }
    else
      problems << "no family-mruby_*_#{arch}.deb to compare with the staging"
    end
    secrets = wasm_local_secrets
    files.each do |f|
      blob = File.binread(f)
      problems << "a local credential appears in #{f.sub("#{pkg}/", '')}" if secrets.any? { |s| blob.include?(s) }
      # A PEM key is its header followed by the key itself: the bare headers
      # are in the program as the TLS library's parser constants.
      { "OpenAI-style key" => /sk-[A-Za-z0-9_-]{20}/n,
        "PEM private key" => %r{BEGIN [A-Z ]*PRIVATE KEY-----\s*[A-Za-z0-9+/=]{40}}n }.each do |what, re|
        problems << "#{what} in #{f.sub("#{pkg}/", '')}" if blob.b =~ re
      end
    end
    abort "native:scan found #{problems.length} problem(s):\n  " + problems.join("\n  ") unless problems.empty?
    puts "native:scan: #{files.length} files in #{arch}/pkg, #{secrets.length} local credential(s) searched for, " \
         "no untracked file, no credential"
  end
end

# ------------------------------------------------------------ compositor
#
# The compositor (L8, native/wlr/) needs wlroots 0.19, which the developer's
# Ubuntu 22.04 does not have: it is built and run in the amd64 image. The
# build is a development one (the repository's flash/ and the staged /etc,
# as /src in the container), apart from the host's native/build.

NATIVE_WL_DIR = File.join(NATIVE_DIR, "build-wl")

def native_wl_binary = File.join(NATIVE_WL_DIR, "cmake", "fmruby-native")

namespace :native do
  desc "Build the program with the wlroots compositor in the Ubuntu 26.04 image (native/build-wl)"
  task :wl => ["^setup", "hosted:m5gfx", :mrb, :etc] do
    native_image_build("amd64")
    gen = File.join(ROOT_DIR, "main/prebuild_scripts/spinel/gen")
    needed = %w[fft_kernel.c spinel_hello_kernel.c raycast_kernel.c]
    needed += %w[fmrb_kernel_combined.c editor_combined.c] if native_engine == "standard"
    missing = needed.reject { |f| File.exist?(File.join(gen, f)) }
    abort "native:wl: #{missing.join(', ')} missing in #{gen}; run rake spinel:gen" unless missing.empty?
    rel = NATIVE_WL_DIR.sub("#{ROOT_DIR}/", "")
    native_docker("cd components/picoruby-esp32/picoruby && rake -j#{ENV['JOBS'] || 8}",
                  { "FMRB_NATIVE_ARCH" => "amd64", "MRUBY_BUILD_DIR" => "/src/#{rel}/mruby",
                    "MRUBY_CONFIG" => "/src/lib/add/family_mruby_native.rb" }, arch: "amd64")
    native_docker(<<~SH, {}, arch: "amd64")
      cmake -S native -B #{rel}/cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo #{native_engine_defines} \
        -DFMRB_HOSTED_MRUBY_DIR=/src/#{rel}/mruby/family-mruby-native
      cmake --build #{rel}/cmake -j#{ENV['JOBS'] || 8} --target fmruby-native
    SH
    puts "native:wl: #{native_wl_binary}"
  end

  desc "Run the compositor build nested in WSLg (ARGS=\"--set compositor.autostart=foot\")"
  task :wl_run do
    abort "#{native_wl_binary} not found; run rake native:wl first" unless File.exist?(native_wl_binary)
    sh "ruby #{File.join(ROOT_DIR, 'test/native/wl/wl_check.rb')} --run " \
       "#{ENV['ARGS'] ? "-- #{ENV['ARGS']}" : ''}"
  end

  desc "The compositor's checks in a container (WLR_RENDERER=pixman too; NESTED=1 on WSLg)"
  task :wl_check do
    abort "#{native_wl_binary} not found; run rake native:wl first" unless File.exist?(native_wl_binary)
    sh "ruby #{File.join(ROOT_DIR, 'test/native/wl/wl_check.rb')}#{ENV['NESTED'] ? ' --nested' : ''}"
  end
end
