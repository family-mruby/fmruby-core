# The wire format between fmruby-core and fmruby-graphics-audio has one copy:
# components/fmrb_wire/include (the link protocol and its frames, COBS, the
# link version, the HID event packets, the simulation's shared memory).
# graphics-audio's CMake and build container, and the sdl2-display image, read
# it from this checkout instead of keeping their own (doc/codebase_tidy, T2).
#
# `rake wire:check` (part of `rake test`, so of `rake regress`) keeps it that
# way:
#   - every header there compiles on its own, as C and as C++, with nothing
#     but libc: graphics-audio builds them with another IDF and none of this
#     firmware's headers, so an include of fmrb.h or fmrb_err.h would break
#     it there first;
#   - no other file carries one of their names, and FMRB_LINK_VERSION is
#     defined in one place (a copy left behind, or a new one, is what drifted
#     before);
#   - graphics-audio, its docker build and the sdl2-display image still point
#     at this directory;
#   - the PC tools that write the same numbers by hand (tools/fmrb_input.rb,
#     fmrb_screenshot.py, fmrb_audio_probe.rb at the workspace root) still
#     agree with the headers.
# The parts outside this repository are checked when the checkout is there
# (the workspace layout); a lone fmruby-core checkout (CI) checks its own part.

require "open3"

namespace :wire do
  WIRE_DIR = "components/fmrb_wire/include"

  desc "Check the shared wire headers: self-contained, one copy, read by graphics-audio and sdl2-display"
  task :check do
    errors = []
    notes = []
    headers = Dir.glob("#{WIRE_DIR}/*.h").sort
    names = headers.map { |h| File.basename(h) }
    errors << "#{WIRE_DIR} has no headers" if headers.empty?

    # 1. Each header stands on its own.
    cc = ENV["CC"] || "cc"
    cxx = ENV["CXX"] || "c++"
    [[cc, "c", "-std=gnu11"], [cxx, "c++", "-std=gnu++17"]].each do |compiler, lang, std|
      unless system("which #{compiler} > /dev/null 2>&1")
        notes << "#{compiler} not found, #{lang} not compiled"
        next
      end
      names.each do |n|
        out, st = Open3.capture2e(compiler, "-x", lang, std, "-Wall", "-Wextra", "-Werror",
                                  "-fsyntax-only", "-I", WIRE_DIR, "-",
                                  stdin_data: "#include \"#{n}\"\n")
        errors << "#{n} does not compile alone as #{lang}:\n#{out.lines.first(6).join}" unless st.success?
      end
    end

    # 2. One copy. git ls-files leaves the submodules' contents out.
    tracked = lambda do |dir|
      # Untracked files too: a stray copy in graphics-audio's main/common would
      # be found before this directory on its include path.
      out, st = Open3.capture2("git", "-C", dir, "ls-files", "--cached", "--others", "--exclude-standard")
      st.success? ? out.lines.map(&:chomp) : []
    end
    copies = lambda do |dir, label, allowed|
      tracked.call(dir).each do |f|
        next if allowed && f.start_with?("#{allowed}/")
        base = File.basename(f)
        errors << "#{label}#{f}: a copy of the wire header #{base} (the one copy is fmruby-core/#{WIRE_DIR})" if names.include?(base)
        errors << "#{label}#{f}: a copy of fmrb_link_cobs.c (graphics-audio builds fmruby-core's)" if label != "" && base == "fmrb_link_cobs.c"
      end
    end
    define_elsewhere = lambda do |dir, label|
      files = tracked.call(dir).select { |f| f =~ /\.(h|c|cpp|hpp)\z/ }
      files.each do |f|
        next if f == "#{WIRE_DIR}/fmrb_link_version.h"
        path = File.join(dir, f)
        next unless File.file?(path)
        if File.read(path, encoding: "BINARY").match?(/^\s*#\s*define\s+FMRB_LINK_VERSION\b/)
          errors << "#{label}#{f}: defines FMRB_LINK_VERSION (the one place is #{WIRE_DIR}/fmrb_link_version.h)"
        end
      end
    end
    copies.call(".", "", WIRE_DIR)
    define_elsewhere.call(".", "")

    # 3. The other side reads this directory.
    workspace = File.expand_path("..", Dir.pwd)
    ga = File.join(workspace, "fmruby-graphics-audio")
    if File.directory?(File.join(ga, "main"))
      copies.call(ga, "fmruby-graphics-audio/", nil)
      define_elsewhere.call(ga, "fmruby-graphics-audio/")
      expect = lambda do |file, pattern, what|
        path = File.join(workspace, file)
        text = File.exist?(path) ? File.read(path) : ""
        errors << "#{file}: #{what}" unless text.match?(pattern)
      end
      expect.call("fmruby-graphics-audio/main/CMakeLists.txt", %r{fmruby-core.*\n(.*\n)*.*components/fmrb_wire/include},
                  "does not put fmruby-core's #{WIRE_DIR} on the include path")
      expect.call("fmruby-graphics-audio/Rakefile", %r{:/fmruby-core:ro},
                  "the build container does not mount fmruby-core read-only at /fmruby-core")
      expect.call("fmruby-graphics-audio/sdl2-display/Dockerfile", /COPY --from=wire\b/,
                  "the sdl2-display image does not copy the wire headers from the \"wire\" context")
      expect.call("docker-compose.yml", %r{wire:\s*\./fmruby-core/#{Regexp.escape(WIRE_DIR)}\b},
                  "sdl2-display has no additional context \"wire\" at ./fmruby-core/#{WIRE_DIR}")
      expect.call("docker-compose.yml", %r{\./fmruby-core:/fmruby-core:ro},
                  "the graphics-audio service does not mount fmruby-core read-only at /fmruby-core")
    else
      notes << "no fmruby-graphics-audio next to this checkout, its side not checked"
    end

    # 4. Numbers the PC tools write by hand.
    define = lambda do |header, name|
      m = File.read(File.join(WIRE_DIR, header)).match(/^#define\s+#{name}\s+(\S+)/)
      return nil unless m
      v = m[1]
      v.start_with?("\"") ? v.delete("\"") : Integer(v)
    end
    tool_value = lambda do |file, name|
      path = File.join(workspace, file)
      return :missing unless File.exist?(path)
      m = File.read(path).match(/^\s*#{name}\s*=\s*("[^"]*"|0x\h+|\d+)/)
      return nil unless m
      m[1].start_with?("\"") ? m[1].delete("\"") : Integer(m[1])
    end
    pairs = [
      ["tools/fmrb_input.rb", "EV_KEY_DOWN", "fmrb_hid_event.h", "HID_EVENT_KEY_DOWN"],
      ["tools/fmrb_input.rb", "EV_KEY_UP", "fmrb_hid_event.h", "HID_EVENT_KEY_UP"],
      ["tools/fmrb_input.rb", "EV_MOUSE_BUTTON", "fmrb_hid_event.h", "HID_EVENT_MOUSE_BUTTON"],
      ["tools/fmrb_input.rb", "EV_MOUSE_MOTION", "fmrb_hid_event.h", "HID_EVENT_MOUSE_MOTION"],
      ["tools/fmrb_input.rb", "EV_MOUSE_WHEEL", "fmrb_hid_event.h", "HID_EVENT_MOUSE_WHEEL"],
      ["tools/fmrb_input.rb", "EV_IME_TEXT", "fmrb_hid_event.h", "HID_EVENT_IME_TEXT"],
      ["tools/fmrb_input.rb", "SOCKET_PATH", "shm_display.h", "FMRB_INJECT_SOCKET_PATH"],
      ["tools/fmrb_screenshot.py", "READY_MAGIC", "shm_display.h", "FMRB_SHM_READY_MAGIC"],
      ["tools/fmrb_screenshot.py", "LEGACY_MAGIC_V1", "shm_display.h", "FMRB_SHM_LEGACY_MAGIC_V1"],
      ["tools/fmrb_screenshot.py", "LAYOUT_VERSION", "shm_display.h", "FMRB_SHM_LAYOUT_VERSION"],
      ["tools/fmrb_screenshot.py", "FB_MAX_W", "shm_display.h", "FMRB_SHM_HIRES_WIDTH"],
      ["tools/fmrb_screenshot.py", "FB_MAX_H", "shm_display.h", "FMRB_SHM_HIRES_HEIGHT"],
      ["tools/fmrb_audio_probe.rb", "SAMPLE_RATE", "shm_display.h", "FMRB_SHM_AUDIO_SAMPLE_RATE"],
    ]
    skipped_tools = []
    pairs.each do |file, tname, header, hname|
      want = define.call(header, hname)
      got = tool_value.call(file, tname)
      if got == :missing
        skipped_tools << file
      elsif want.nil?
        errors << "#{header}: no #{hname} (#{file} #{tname} is checked against it)"
      elsif got != want
        errors << "#{file}: #{tname} = #{got.inspect}, #{header} #{hname} = #{want.inspect}"
      end
    end
    notes << "not found, not compared: #{skipped_tools.uniq.join(', ')}" unless skipped_tools.empty?

    notes.each { |n| puts "wire: #{n}" }
    unless errors.empty?
      errors.each { |e| puts "wire: FAIL #{e}" }
      abort "wire: #{errors.size} problem(s)"
    end
    puts "wire: #{names.size} headers self-contained, one copy, #{pairs.size} tool numbers agree"
  end
end
