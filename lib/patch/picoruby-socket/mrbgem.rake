# Family mruby patch of picoruby-socket/mrbgem.rake.
#
# Upstream only recognizes the ESP-IDF build as build.name == "esp32"; the
# Family mruby Modern target builds libmruby.a under the name "esp32p4".
# Without this patch the esp32p4 build would clone its own LwIP tree and
# compile it (rp2040/cyw43 path), which conflicts with ESP-IDF's LwIP.
#
# Changes from upstream:
# - esp_idf_build covers esp32 / esp32-microruby / esp32p4
# - ESP-IDF builds define PICORB_PLATFORM_POSIX for the gem's own sources so
#   picorb_socket_t has the same (fd-based) layout as ports/esp32/*.c, which
#   are compiled by the picoruby-esp32 IDF component against ESP-IDF LwIP.
MRuby::Gem::Specification.new('picoruby-socket') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'CRuby-compatible Socket implementation for PicoRuby'
  spec.description = 'Provides TCPSocket, UDPSocket, and TCPServer classes compatible with CRuby'

  spec.require_name = 'socket'

  esp_idf_build = %w[esp32 esp32-microruby esp32p4].include?(build.name)

  # Dependencies
  unless build.posix?
    spec.add_dependency 'picoruby-mbedtls'  # SSL/TLS support for non-POSIX platforms
  end
  spec.add_conflict 'picoruby-net'

  # Add include directory
  spec.cc.include_paths << "#{dir}/include"

  if esp_idf_build
    # Match the struct layout used by ports/esp32/*.c (BSD sockets on LwIP).
    # The bare-metal toolchain has no <sys/socket.h>; src/mruby/socket.c is
    # patched (lib/patch) to use its fallback constants when ESP32_PLATFORM
    # is defined.
    spec.cc.defines << 'PICORB_PLATFORM_POSIX=1'
  end

  # Add mbedtls include path for SSL support (non-POSIX only)
  unless build.posix? || esp_idf_build
    mbedtls_dir = "#{MRUBY_ROOT}/mrbgems/picoruby-mbedtls/lib/mbedtls"
    if File.directory?(mbedtls_dir)
      spec.cc.include_paths << "#{mbedtls_dir}/include"
    end
  end

  unless build.posix? || esp_idf_build
    # LwIP configuration
    LWIP_VERSION = "STABLE-2_2_1_RELEASE"
    LWIP_REPO = "https://github.com/lwip-tcpip/lwip"
    lwip_dir = "#{dir}/lib/lwip"

    # Clone or update LwIP repository
    if File.symlink?(lwip_dir)
      # Symlink to pico-sdk's lwip (used in R2P2 builds)
      # Note: This modifies a submodule's submodule. To ignore the changes in git status,
      # add 'ignore = dirty' to picoruby-r2p2/lib/pico-sdk in .gitmodules
      unless File.directory?(lwip_dir)
        raise "Symlink #{lwip_dir} exists but target is missing. Run: rake r2p2:setup"
      end
    elsif File.directory?(lwip_dir)
      if File.directory?("#{lwip_dir}/.git")
        current_branch = `cd #{lwip_dir} && git branch --show-current 2>/dev/null`.strip
        current_tag = `cd #{lwip_dir} && git describe --tags --exact-match HEAD 2>/dev/null`.strip

        unless current_branch == LWIP_VERSION || current_tag == LWIP_VERSION
          puts "lwip version mismatch. Fetching and checking out #{LWIP_VERSION}..."
          sh "cd #{lwip_dir} && git fetch origin #{LWIP_VERSION}:#{LWIP_VERSION} 2>/dev/null || git fetch origin"
          sh "cd #{lwip_dir} && git checkout #{LWIP_VERSION}"
        end
      else
        puts "lwip directory exists but is not a git repository. Removing and cloning..."
        FileUtils.rm_rf(lwip_dir)
        sh "git clone -b #{LWIP_VERSION} #{LWIP_REPO} #{lwip_dir}"
      end
    else
      sh "git clone -b #{LWIP_VERSION} #{LWIP_REPO} #{lwip_dir}"
    end

    # Apply patches to LwIP
    patch_file = "#{dir}/patches/lwip-altcp-proxyconnect.patch"
    if File.exist?(patch_file)
      proxyconnect_file = "#{lwip_dir}/src/apps/http/altcp_proxyconnect.c"
      if File.exist?(proxyconnect_file)
        patch_applied = `cd #{lwip_dir} && git apply --check #{patch_file} 2>&1`.strip
        if patch_applied.empty?
          sh "cd #{lwip_dir} && git apply #{patch_file}"
          puts "Applied patch: lwip-altcp-proxyconnect.patch"
        end
      end
    end

    spec.cc.defines << 'PICO_CYW43_ARCH_POLL=1'

    # Add LwIP include paths
    spec.cc.include_paths << "#{lwip_dir}/src/include"
    spec.cc.include_paths << "#{lwip_dir}/contrib/ports/unix/port/include"
    spec.cc.include_paths << "#{lwip_dir}/src/apps/altcp_tls"

    # Compile LwIP source files
    Dir.glob("#{lwip_dir}/src/**/*.c").each do |src|
      next if src.end_with?('makefsdata.c')
      next if src.end_with?('altcp_tls_mbedtls.c')  # Use custom version from ports/rp2040
      obj = src.relative_path_from(dir).pathmap("#{build_dir}/%X.o")
      spec.objs << obj
    end
  end
end
