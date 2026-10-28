#!/usr/bin/env ruby
# frozen_string_literal: true
#
# session_test.rb - the login screen's session starter
# (native/package/family-mruby-session, L9b, L9c) on the host: how it picks
# the renderer in a virtual machine.
#
# The script runs from a temporary directory beside a stand-in for the
# program (it prints the environment it was given and its arguments), with
# systemd-detect-virt replaced by a stand-in that answers what each case
# needs (or is missing). No display, no wlroots, nothing installed.
#
#   ruby test/native/session_test.rb
#
# Cases: a virtual machine without settings (pixman and the software pointer,
# the reason in one line); the settings file naming the GPU renderer (it
# wins, the pointer still in software); both set by the settings or the
# environment (kept); not a virtual machine (nothing set, nothing said);
# no systemd-detect-virt (as not a virtual machine).
#
# And (L14) starting it again: a stand-in that ends as a plan says (SIGKILL,
# SIGUSR1, an exit status, SIGTERM, a normal end) on each run: signals that
# leave no core (on WSL a core goes to the host's crash capture whatever the
# limit; the compositor's check ends the real program with SIGSEGV). A crash
# is followed by a new run told how the last ended (FMRB_SESSION_RESTARTED);
# the 4th abnormal end within a minute ends the session (status 1); a
# normal end (the Exit entry) and SIGTERM are never followed by a run;
# SIGTERM to the session itself goes to the program and ends the session.

require "fileutils"
require "open3"
require "tmpdir"

CORE = File.expand_path("../..", __dir__)
SCRIPT = File.join(CORE, "native/package/family-mruby-session")

$fail = 0
$count = 0

def check(cond, what)
  $count += 1
  return if cond

  $fail += 1
  puts "FAIL #{what}"
end

Dir.mktmpdir("fmrb-session-test") do |dir|
  bin = File.join(dir, "bin")
  FileUtils.mkdir_p(bin)
  FileUtils.cp(SCRIPT, File.join(bin, "family-mruby-session"))
  File.write(File.join(bin, "family-mruby"),
             "#!/bin/sh\necho \"R=${WLR_RENDERER:-} C=${WLR_NO_HARDWARE_CURSORS:-} ARGS=$*\"\n")
  File.chmod(0o755, File.join(bin, "family-mruby"), File.join(bin, "family-mruby-session"))

  # The tools the script needs, and nothing else, on a PATH of its own.
  tools = File.join(dir, "tools")
  FileUtils.mkdir_p(tools)
  %w[dirname readlink].each do |t|
    path = %w[/usr/bin /bin].map { |d| File.join(d, t) }.find { |f| File.executable?(f) }
    abort "#{t} not found" unless path
    File.symlink(path, File.join(tools, t))
  end

  # A systemd-detect-virt answering `answer` (exit 1 for "none", as the real
  # one does), or none at all.
  run = lambda do |answer, conf: nil, env: {}|
    virt = File.join(dir, "virt")
    FileUtils.rm_rf(virt)
    FileUtils.mkdir_p(virt)
    if answer
      code = answer == "none" ? 1 : 0
      File.write(File.join(virt, "systemd-detect-virt"),
                 "#!/bin/sh\n[ \"$1\" = --vm ] || exit 2\necho #{answer}\nexit #{code}\n")
      File.chmod(0o755, File.join(virt, "systemd-detect-virt"))
    end
    conf_file = File.join(dir, "session.conf")
    File.write(conf_file, conf.to_s)
    e = { "PATH" => "#{virt}:#{tools}", "FMRB_SESSION_CONF" => conf_file, "WLR_BACKENDS" => "headless",
          "WLR_RENDERER" => nil, "WLR_NO_HARDWARE_CURSORS" => nil }.merge(env)
    out, err, st = Open3.capture3(e, "/bin/sh", File.join(bin, "family-mruby-session"), unsetenv_others: false)
    [out.strip, err, st.success?]
  end

  out, err, ok = run.call("oracle")
  check(ok && out.start_with?("R=pixman C=1 ARGS=--machine linux_pc_wl"), "VM, no settings: #{out}")
  check(err.include?("a virtual machine (oracle): drawing without the GPU (WLR_RENDERER=pixman " \
                     "WLR_NO_HARDWARE_CURSORS=1)"), "VM, no settings: the line (#{err.lines.first})")

  out, err, ok = run.call("vmware", conf: "WLR_RENDERER=gles2\n")
  check(ok && out.start_with?("R=gles2 C=1 "), "VM, the settings name gles2: #{out}")
  check(err.include?("(vmware): drawing without the GPU (WLR_NO_HARDWARE_CURSORS=1; kept as set: " \
                     "WLR_RENDERER=gles2)"), "VM, gles2: the line (#{err.lines.first})")

  out, err, ok = run.call("kvm", conf: "WLR_RENDERER=gles2\nWLR_NO_HARDWARE_CURSORS=0\n")
  check(ok && out.start_with?("R=gles2 C=0 "), "VM, both set: #{out}")
  check(err.include?("a virtual machine (kvm): kept as set: WLR_RENDERER=gles2 WLR_NO_HARDWARE_CURSORS=0"),
        "VM, both set: the line (#{err.lines.first})")

  out, err, ok = run.call("qemu", env: { "WLR_RENDERER" => "vulkan" })
  check(ok && out.start_with?("R=vulkan C=1 "), "VM, the environment names vulkan: #{out}")

  out, err, ok = run.call("none")
  check(ok && out.start_with?("R= C= "), "not a VM: #{out}")
  check(!err.include?("virtual machine"), "not a VM: nothing said (#{err.lines.first})")

  out, err, ok = run.call("none", conf: "WLR_RENDERER=pixman\n")
  check(ok && out.start_with?("R=pixman C= "), "not a VM, the settings name pixman: #{out}")

  out, err, ok = run.call(nil)
  check(ok && out.start_with?("R= C= ") && !err.include?("virtual machine"), "no systemd-detect-virt: #{out}")

  out, = run.call("oracle", conf: "ROTATE=90\n")
  check(out.include?("--set display.rotate=90") && out.start_with?("R=pixman "), "VM and ROTATE: #{out}")
end

Dir.mktmpdir("fmrb-session-restart") do |dir|
  bin = File.join(dir, "bin")
  FileUtils.mkdir_p(bin)
  FileUtils.cp(SCRIPT, File.join(bin, "family-mruby-session"))
  plan = File.join(dir, "plan")
  # The stand-in: one line of the plan per run.
  File.write(File.join(bin, "family-mruby"), <<~SH)
    #!/bin/sh
    n=$(cat "#{plan}.n" 2> /dev/null || echo 0)
    n=$((n + 1))
    echo "$n" > "#{plan}.n"
    echo "run $n [$FMRB_SESSION_RESTARTED]" >> "#{plan}.log"
    act=$(sed -n "${n}p" "#{plan}")
    case "$act" in
      kill) kill -KILL $$ ;;
      usr1) kill -USR1 $$ ;;
      term) kill -TERM $$ ;;
      exit3) exit 3 ;;
      hold) trap 'echo "got TERM" >> "#{plan}.log"; exit 0' TERM; sleep 30 & wait ;;
      *) exit 0 ;;
    esac
  SH
  File.chmod(0o755, File.join(bin, "family-mruby"), File.join(bin, "family-mruby-session"))
  tools = File.join(dir, "tools")
  FileUtils.mkdir_p(tools)
  %w[dirname readlink cat sed date sleep].each do |t|
    path = %w[/usr/bin /bin].map { |d| File.join(d, t) }.find { |f| File.executable?(f) }
    abort "#{t} not found" unless path
    File.symlink(path, File.join(tools, t))
  end
  env = { "PATH" => tools, "FMRB_SESSION_CONF" => File.join(dir, "none.conf"), "WLR_BACKENDS" => "headless" }
  restart = lambda do |*acts|
    File.write(plan, acts.join("\n") + "\n")
    FileUtils.rm_f(["#{plan}.n", "#{plan}.log"])
    _out, err, st = Open3.capture3(env, "/bin/sh", File.join(bin, "family-mruby-session"), unsetenv_others: true)
    runs = File.exist?("#{plan}.log") ? File.readlines("#{plan}.log", chomp: true) : []
    [runs, err, st]
  end

  runs, err, st = restart.call("kill", "exit")
  check(st.exitstatus == 0 && runs == ["run 1 []", "run 2 [SIGKILL]"],
        "a crash, then the Exit: #{runs.inspect} (#{st.exitstatus})")
  check(err.include?("Family mruby ended by SIGKILL; starting it again (1 of 3 within 60s)") &&
        err.include?("Family mruby ended (its Exit entry)"), "a crash, then the Exit: the lines (#{err.lines.last(3).join.strip})")

  runs, err, st = restart.call("kill", "usr1", "exit3", "kill", "exit")
  check(st.exitstatus == 1 && runs.size == 4 && runs[1] == "run 2 [SIGKILL]" &&
        runs[2] == "run 3 [SIGUSR1]" && runs[3] == "run 4 [status 3]",
        "the 4th abnormal end in a minute: #{runs.inspect} (#{st.exitstatus})")
  check(err.include?("that is 4 abnormal ends within 60s: not starting it again"),
        "the 4th abnormal end: the line (#{err.lines.last.to_s.strip})")

  runs, err, st = restart.call("exit")
  check(st.exitstatus == 0 && runs == ["run 1 []"], "the Exit alone: #{runs.inspect} (#{st.exitstatus})")

  runs, err, st = restart.call("term", "exit")
  check(st.exitstatus == 143 && runs == ["run 1 []"] && err.include?("ended by SIGTERM: not a crash"),
        "SIGTERM to the program: #{runs.inspect} (#{st.exitstatus}, #{err.lines.last.to_s.strip})")

  # SIGTERM to the session (a logout): the program gets it, no new run.
  File.write(plan, "hold\nexit\n")
  FileUtils.rm_f(["#{plan}.n", "#{plan}.log"])
  r, w = IO.pipe
  pid = Process.spawn(env, "/bin/sh", File.join(bin, "family-mruby-session"), err: w, out: File::NULL,
                      unsetenv_others: true)
  w.close
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 10
  sleep 0.1 until File.exist?("#{plan}.log") || Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline
  sleep 0.3
  Process.kill("TERM", pid)
  _, st = Process.wait2(pid)
  err = r.read
  runs = File.readlines("#{plan}.log", chomp: true)
  check(runs == ["run 1 []", "got TERM"] && err.include?("the session ends (SIGTERM)"),
        "SIGTERM to the session: #{runs.inspect} (#{st.exitstatus}, #{err.lines.last.to_s.strip})")
end

puts "session: #{$count - $fail}/#{$count} checks passed (the renderer in a virtual machine, starting again after a crash)"
exit($fail.zero? ? 0 : 1)
