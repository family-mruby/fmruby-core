#!/usr/bin/env ruby
# frozen_string_literal: true
#
# run.rb - draw every scene on every profile and compare with the baseline
# (doc/regression/usage.md).
#
#   ruby tools/regress/run.rb                 # compare (rake regress runs this)
#   ruby tools/regress/run.rb --update        # write the baseline (rake regress:update)
#   ruby tools/regress/run.rb --only othello  # scenes whose id contains "othello"
#   ruby tools/regress/run.rb --list          # the scenes and the profiles they run on
#   ruby tools/regress/run.rb -j 1            # one run at a time (default: one per CPU)
#   ruby tools/regress/run.rb --debug board/list@modern-426x240   # one run here, errors shown
#
# Each run (one scene on one profile) is a forked process: it loads the
# stand-ins, the gem's Ruby and the app, plays the scene and writes its
# record to tmp/regress/out/. The comparison prints a short summary; the
# whole diff goes to tmp/regress/diff.txt. Exit status 1 when anything
# differs, a run fails, or a baseline has no scene any more.
require "fileutils"
require "etc"
require "open3"
require "zlib"
require_relative "targets"
require_relative "scene"
require_relative "scene_helpers"

module Regress
  BASELINE = File.join(CORE, "test", "regress", "baseline")
  OUT = File.join(CORE, "tmp", "regress")
  SCENE_DIR = File.join(__dir__, "scenes")
  GEM_APP = File.join(CORE, "lib", "add", "picoruby-fmrb-app", "mrblib")
  GEM_AST = File.join(CORE, "lib", "add", "picoruby-fmrb-asterism", "mrblib")

  Profile = Struct.new(:name, :target, :boards) do
    def targets = boards.map(&:name)
  end
  Job = Struct.new(:scene, :profile) do
    def key = "#{scene.app.name}/#{scene.name}@#{profile.name}"
    def file = "#{scene.app.name}/#{scene.name.tr('/', '_')}@#{profile.name}.txt"
  end

  class << self
    attr_accessor :app_class

    # The profiles, named by family and screen ("modern-426x240"); one that
    # shares those with another gets what tells them apart appended.
    def profiles
      ps = TARGETS.group_by(&:profile_key).map do |_k, ts|
        t = ts[0]
        Profile.new("#{t.family}-#{t.screen[0]}x#{t.screen[1]}", t, ts)
      end
      ps.group_by(&:name).each_value do |same|
        next if same.size < 2
        same.each do |p|
          t = p.target
          p.name += "-#{t.hires ? "hires#{t.hires.join('x')}" : 'nohires'}-#{t.font_ja ? 'ja' : 'noja'}" \
                    "-m#{t.margin.join('x')}-a#{t.app.join('x')}"
        end
      end
      ps
    end

    def jobs(only)
      ps = profiles
      scenes.flat_map do |s|
        ps.select { |p| s.applies?(p) }.map { |p| Job.new(s, p) }
      end.select { |j| only.nil? || j.key.include?(only) }
    end

    def tracked
      out, st = Open3.capture2("git", "-C", CORE, "ls-files", "-z", "flash")
      return out.split("\0") if st.success? && !out.empty?
      Dir.glob("flash/**/*", base: CORE).select { |f| File.file?(File.join(CORE, f)) }
    end

    # ------------------------------------------------------------ one run

    # In the forked child: everything from a clean Ruby to the record.
    def run_child(job, tracked, out_path)
      t = job.profile.target
      s = job.scene
      a = s.app
      overlay = File.join(OUT, "fs", job.file.sub(/\.txt\z/, ""))
      FileUtils.rm_rf(overlay)
      FileUtils.mkdir_p(overlay)
      require_relative "fake_fs"
      require_relative "stand_ins"
      require_relative "fake_asterism"
      require_relative "mini_toml"
      srand(Zlib.crc32(s.id))
      conf_text = File.read(File.join(CORE, "config", t.conf))
      conf = MiniToml.parse(conf_text)
      unless conf["display_width"] == t.screen[0] && conf["display_height"] == t.screen[1]
        raise "targets.rb: #{t.name} says #{t.screen.join('x')}, config/#{t.conf} says " \
              "#{conf['display_width']}x#{conf['display_height']}"
      end
      device = a.file.to_s.sub(%r{\Aflash/}, "/").sub(%r{\Amain/prebuild_scripts/default_app/}, "/app/system/")
      Env.reset(t, s.lang, device)
      Net.reset
      define_const(t, s.lang, conf["theme"] || {})
      FakeFS.setup(CORE, overlay, tracked)
      FakeFS.active = true
      FakeFS.seed("/etc/system_conf.toml", conf_text.sub(/^language\s*=.*$/, "language = \"#{s.lang}\""))
      FakeFS.seed("/home/asterism/config.toml",
                  "router = \"tcp/192.0.2.1:7447\"\nnode = \"fmruby-a1b2c3\"\nname = \"Player\"\n")
      Env.extra[:window] = window_of(a, t)
      # The C half first, then the Ruby over it, as the firmware defines them.
      load File.join(__dir__, "recorder.rb")
      gem_files(GEM_APP).each { |f| load_file(f) unless File.basename(f) == "gfx_block.rb" }
      a.gems.each { |g| gem_files(File.join(CORE, "lib", "add", g, "mrblib")).each { |f| load_file(f) } }
      gem_files(GEM_AST).sort_by { |f| File.basename(f) == "fmrb-asterism.rb" ? 0 : 1 }.each { |f| load_file(f) }
      session = Session.new(s, t)
      a.setups.each { |b| b.call(session) }
      a.prelude.each { |b| b.call(session) }
      Regress.app_class = load_app(a)
      Rec.reset
      s.body.call(session)
      w = Env.extra[:window]
      head = ["# scene #{s.id}", "# profile #{job.profile.name}",
              "# window #{w[:w]}x#{w[:h]}#{w[:fullscreen] ? ' fullscreen' : ''} lang #{s.lang}"]
      tail = Env.unstubbed.sort.map { |n| "# unstubbed: #{n}" }
      File.__regress_open(out_path, "w") { |f| f.puts(head + Rec.lines + tail) }
    end

    def gem_files(dir) = Dir.__regress_glob(File.join(dir, "*.rb")).sort

    def load_file(path)
      FakeFS.active = false
      src = File.read(path)
      FakeFS.active = true
      eval(src, TOPLEVEL_BINDING, path) # rubocop:disable Security/Eval
    end

    # The window the spawner gives the app (fmrb_app_spawner.c): the .app.toml
    # next to the file, else the app's own window line, else the default.
    def window_of(a, t)
      w = a.window
      toml_path = File.join(CORE, a.file.to_s.sub(/\.rb\z/, ".toml"))
      if w.nil? && File.__regress_exist?(toml_path)
        c = MiniToml.parse(File.read(toml_path))
        mode = c["default_window_mode"].to_s
        w = { name: c["app_screen_name"] || File.basename(a.file).split(".")[0], fullscreen: mode == "fullscreen",
              headless: mode == "background", hires: c["fullscreen_hires"] == true || c["fullscreen_hires"].to_i != 0,
              w: c["default_window_width"] || 100, h: c["default_window_height"] || 100,
              x: c["default_window_pos_x"] || 50, y: c["default_window_pos_y"] || 50,
              rounded: c.fetch("rounded_corners", 1).to_i != 0 }
      end
      w ||= { name: File.basename(a.file.to_s).split(".")[0], w: t.app[0], h: t.app[1] }
      w = { fullscreen: false, headless: false, hires: false, x: 50, y: 50, rounded: true }.merge(w)
      w[:win_w] = w[:w]
      w[:win_h] = w[:h]
      if w[:fullscreen]
        w[:w], w[:h] = t.fullscreen_size(w[:hires])
        w[:x] = w[:y] = 0
      end
      w
    end

    # Reads the app's file without its start code (the last `begin` that
    # makes the app and starts it); returns the class name to make.
    def load_app(a)
      if a.inline
        a.inline.call
        return a.klass
      end
      path = File.join(CORE, a.file)
      lines = File.read(path).lines
      cut = (0...lines.size).to_a.reverse.find { |i| lines[i] =~ /\Abegin\b/ && lines[i..].join.include?(".start") }
      raise "#{a.file}: no start code (begin ... .start) found" if cut.nil?
      trailer = lines[cut..].join
      cls = a.klass || trailer[/(\b[A-Z]\w*(?:::\w+)*)\.new/, 1]
      raise "#{a.file}: cannot tell the app's class" if cls.nil?
      eval(lines[0...cut].join, TOPLEVEL_BINDING, path) # rubocop:disable Security/Eval
      cls
    end

    # ------------------------------------------------------------ the batch

    def run_all(js, jobs_n, tracked)
      out_dir = File.join(OUT, "out")
      FileUtils.rm_rf(out_dir)
      FileUtils.rm_rf(File.join(OUT, "fs"))
      results = {}
      queue = js.dup
      running = {}
      until queue.empty? && running.empty?
        while running.size < jobs_n && !queue.empty?
          j = queue.shift
          path = File.join(out_dir, j.file)
          err = path.sub(/\.txt\z/, ".err")
          FileUtils.mkdir_p(File.dirname(path))
          pid = fork do
            begin
              $stdout.reopen(File::NULL)
              run_child(j, tracked, path)
              exit!(0)
            rescue Exception => e # rubocop:disable Lint/RescueException
              bt = e.backtrace.to_a.reject { |l| l.include?("tools/regress/") }.first(4)
              File.__regress_open(err, "w") { |f| f.puts("#{e.class}: #{e.message}", *bt.map { |l| "  #{l.sub(CORE + '/', '')}" }) }
              exit!(2)
            end
          end
          running[pid] = [j, path, err]
        end
        pid, st = Process.wait2
        j, path, err = running.delete(pid)
        results[j.key] = if st.success? && File.exist?(path)
                           [:ok, path]
                         else
                           [:error, File.exist?(err) ? File.read(err) : "exit #{st.exitstatus}"]
                         end
      end
      results
    end

    def diff(a, b)
      out, = Open3.capture2("diff", "-u", "--label", "baseline", "--label", "now", a, b)
      out
    end

    def main(argv)
      update = false
      only = nil
      jobs_n = Etc.nprocessors
      list = false
      debug = nil
      until argv.empty?
        x = argv.shift
        case x
        when "--update" then update = true
        when "--only" then only = argv.shift
        when "-j" then jobs_n = argv.shift.to_i
        when "--list" then list = true
        when "--debug" then debug = argv.shift
        else abort "unknown argument #{x}"
        end
      end
      Dir.glob(File.join(SCENE_DIR, "*.rb")).sort.each { |f| require f }
      js = jobs(only)
      if debug
        # One run in this process, its output and errors on the terminal.
        j = js.find { |x| x.key == debug } or abort("no run #{debug} (see --list)")
        path = File.join(OUT, "debug.txt")
        FileUtils.mkdir_p(OUT)
        run_child(j, tracked, path)
        puts "record: #{path.sub(CORE + '/', '')}"
        return 0
      end
      if list
        profiles.each { |p| puts "profile #{p.name}: #{p.targets.join(' ')}" }
        js.each { |j| puts j.key }
        return 0
      end
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      results = run_all(js, [jobs_n, 1].max, tracked)
      took = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
      wanted = js.map { |j| File.join(BASELINE, j.file) }
      orphans = only ? [] : Dir.glob(File.join(BASELINE, "**", "*.txt")).sort - wanted
      errors = js.select { |j| results[j.key][0] == :error }
      update ? do_update(js, results, errors, orphans, took) : do_compare(js, results, errors, orphans, took)
    end

    def do_update(js, results, errors, orphans, took)
      changed = []
      js.each do |j|
        next unless results[j.key][0] == :ok
        dst = File.join(BASELINE, j.file)
        src = results[j.key][1]
        next if File.exist?(dst) && File.binread(dst) == File.binread(src)
        changed << "#{File.exist?(dst) ? 'updated' : 'added'}  #{j.key}"
        FileUtils.mkdir_p(File.dirname(dst))
        FileUtils.cp(src, dst)
      end
      orphans.each do |o|
        File.delete(o)
        changed << "removed  #{o.sub(BASELINE + '/', '')}"
      end
      changed.each { |c| puts c }
      errors.each { |j| puts "ERROR  #{j.key}: #{results[j.key][1].lines.first.to_s.strip}" }
      puts format("regress:update: %d runs, %d changed, %d errors (%.1f s)", js.size, changed.size, errors.size, took)
      errors.empty? ? 0 : 1
    end

    def do_compare(js, results, errors, orphans, took)
      failed = []
      full = []
      js.each do |j|
        st, v = results[j.key]
        base = File.join(BASELINE, j.file)
        if st == :error
          failed << [j.key, v.lines.first(3).map(&:rstrip)]
          full << "=== #{j.key}: the run failed\n#{v}"
        elsif !File.exist?(base)
          failed << [j.key, ["no baseline (rake regress:update after checking the record #{v.sub(CORE + '/', '')})"]]
          full << "=== #{j.key}: no baseline\n"
        elsif File.binread(base) != File.binread(v)
          d = diff(base, v)
          body = d.lines.drop(2)
          failed << [j.key, body.reject { |l| l.start_with?(" ") || l.start_with?("@@") }.first(4).map(&:rstrip)]
          full << "=== #{j.key}\n#{d}"
        end
      end
      orphans.each do |o|
        k = o.sub(BASELINE + "/", "")
        failed << [k, ["a baseline with no scene (rake regress:update removes it)"]]
        full << "=== #{k}: a baseline with no scene\n"
      end
      FileUtils.mkdir_p(OUT)
      diff_path = File.join(OUT, "diff.txt")
      File.write(diff_path, full.join("\n"))
      failed.first(3).each do |k, lines|
        puts "FAIL #{k}"
        lines.each { |l| puts "     #{l}" }
      end
      puts "     ... and #{failed.size - 3} more: #{failed.drop(3).map(&:first).first(8).join(' ')}#{failed.size > 11 ? ' ...' : ''}" if failed.size > 3
      puts format("regress: %d runs (%d scenes on %d profiles), %d passed, %d failed (%.1f s)%s",
                  js.size, js.map { |j| j.scene.id }.uniq.size, js.map { |j| j.profile.name }.uniq.size,
                  js.size - (failed.size - orphans.size), failed.size, took,
                  failed.empty? ? "" : "; full diff: #{diff_path.sub(CORE + '/', '')}")
      failed.empty? ? 0 : 1
    end
  end
end

exit(Regress.main(ARGV.dup)) if $PROGRAM_NAME == __FILE__
