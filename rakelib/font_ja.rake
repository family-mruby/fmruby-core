# rakelib/font_ja.rake
# The Japanese font image for the ESP32-P4's font partition (doc/flash_layout F4).
#
#   rake font_ja:image    # tmp/font_ja/font_ja.bin from M5GFX's efontJA arrays (rake build:esp32 runs it on a P4)
#   rake font_ja:test     # the firmware's reader (font_ja_image.c) against images the tool makes (host cc)
#
# The image is made on the host (the build container has no Ruby) before the
# ESP-IDF configure, which needs it: the top-level CMakeLists.txt copies it
# into build/ and adds it to `idf.py flash` (not to app-flash).

FONT_JA_DIR = File.join(ROOT_DIR, "tmp", "font_ja")
FONT_JA_IMAGE = File.join(FONT_JA_DIR, "font_ja.bin")
FONT_JA_TOOL = File.join(ROOT_DIR, "tools", "font_ja", "font_ja_image.rb")

# The M5GFX version main/idf_component.yml pins: the efontJA arrays are read
# from that release, so the image does not move when a newer M5GFX appears.
def font_ja_m5gfx_version
  yml = File.read(File.join(ROOT_DIR, "main", "idf_component.yml"))
  yml[%r{m5stack/m5gfx:\s*\n\s*version:\s*"?([\d.]+)"?}, 1] or
    abort "main/idf_component.yml pins no m5stack/m5gfx version"
end

# lgfx_efont_ja.c of the pinned M5GFX: the component manager's copy when it is
# that version, otherwise fetched once into tmp/font_ja/src/.
def font_ja_source
  ver = font_ja_m5gfx_version
  managed = File.join(ROOT_DIR, "managed_components", "m5stack__m5gfx")
  rel = File.join("src", "lgfx", "Fonts", "efont", "lgfx_efont_ja.c")
  manifest = File.join(managed, "idf_component.yml")
  if File.exist?(manifest) && File.read(manifest)[/^version:\s*"?([\d.]+)/, 1] == ver &&
     File.exist?(File.join(managed, rel))
    return File.join(managed, rel)
  end
  local = File.join(FONT_JA_DIR, "src", "m5gfx-#{ver}", "lgfx_efont_ja.c")
  return local if File.exist?(local)
  require "net/http"
  require "fileutils"
  url = "https://raw.githubusercontent.com/m5stack/M5GFX/#{ver}/#{rel}"
  puts "font_ja: fetching #{url}"
  res = Net::HTTP.get_response(URI(url))
  abort "font_ja: could not fetch #{url} (#{res.code})" unless res.is_a?(Net::HTTPSuccess)
  FileUtils.mkdir_p(File.dirname(local))
  File.binwrite("#{local}.tmp", res.body)
  File.rename("#{local}.tmp", local)
  local
end

namespace :font_ja do
  desc "Make the Japanese font image (tmp/font_ja/font_ja.bin) for the P4's font partition; written only when it changes"
  task :image do
    mkdir_p FONT_JA_DIR, verbose: false
    sh "ruby", FONT_JA_TOOL, "build", font_ja_source, FONT_JA_IMAGE
  end

  FONT_JA_TEST_BUILD = File.join(ROOT_DIR, "test", "font_ja", "build")

  desc "The font image reader (font_ja_image.c) against images the tool writes (host cc + Ruby, no docker)"
  task :test do
    cc = ENV["CC"] || "cc"
    abort "#{cc} not found (install a C compiler or set CC)" unless system("which #{cc} > /dev/null 2>&1")
    mkdir_p FONT_JA_TEST_BUILD, verbose: false
    src = File.join(ROOT_DIR, "main", "drivers", "display_local", "fonts", "font_ja")
    sh "#{cc} -std=c11 -O1 -g -Wall -Wextra -Werror -I #{src} -I components/fmrb_common/include " \
       "#{src}/font_ja_image.c test/font_ja/main.c -o #{FONT_JA_TEST_BUILD}/font_ja_test"
    sh "ruby", File.join(ROOT_DIR, "test", "font_ja", "run.rb"), File.join(FONT_JA_TEST_BUILD, "font_ja_test")
  end
end
