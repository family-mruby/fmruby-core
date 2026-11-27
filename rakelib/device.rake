# rakelib/device.rake
# Device I/O: usbip attach/detach, port detect, flash, check, monitor.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.

# AUTO=1: usbipd attaches again by itself whenever the device comes back
# (usbipd attach --auto-attach). A board whose USB is the chip's own (the
# Tab5's USB Serial/JTAG, 303a:1001) drops off the bus each time the chip
# restarts -- a crash, or a serial port opened with the reset lines -- and
# comes back as a new device that a plain attach does not follow, so the log
# after a crash would be lost. --auto-attach stays running until it is
# stopped, so it is started as a hidden Windows process; rake detach stops it.
def usbipd_attach_cmd(busid)
  if ENV["AUTO"] == "1"
    "powershell.exe -Command \"Start-Process -WindowStyle Hidden usbipd " \
      "-ArgumentList 'attach','--wsl','--busid','#{busid}','--auto-attach'\""
  else
    "powershell.exe -Command \"usbipd attach --wsl --busid #{busid}\""
  end
end

# Stops the hidden auto-attach processes for busid (all of them when nil).
# The command goes as -EncodedCommand (UTF-16LE, base64) so that neither the
# shell nor PowerShell's own quoting touches its $_ and quotes.
def usbipd_stop_auto_attach(busid = nil)
  filter = "$_.CommandLine -match '--auto-attach'"
  filter += " -and $_.CommandLine -match '--busid #{busid}'" if busid
  ps = "$ProgressPreference = 'SilentlyContinue'; " \
       "Get-CimInstance Win32_Process -Filter \"Name='usbipd.exe'\" | " \
       "Where-Object { #{filter} } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force; $_.ProcessId }"
  enc = [ps.encode("UTF-16LE")].pack("m0")
  # PowerShell answers in the console's code page (Japanese on this PC).
  out = `powershell.exe -NoProfile -EncodedCommand #{enc} 2>&1`.force_encoding("UTF-8").scrub.strip
  pids = out.split.grep(/\A\d+\z/)
  if pids.any?
    puts "stopped auto-attach (usbipd pid #{pids.join(', ')})"
  elsif !out.empty?
    warn "could not look for auto-attach processes: #{out.lines.first.to_s.strip}"
  end
end

desc "Attach USB serial devices to WSL2 via usbipd, selected by VID:PID " \
     "(default 1a86:7523 = CH340 and 1a86:55d3 = CH343 (P4-Nano); " \
     "VIDPID=xxxx:yyyy[,xxxx:yyyy] to override, " \
     "BUSID=x-y to force one bus id; AUTO=1 attaches again after each " \
     "restart of the board, until rake detach). Attaches every matching device."
task :attach do
  if ENV["BUSID"]
    sh usbipd_attach_cmd(ENV["BUSID"])
    next
  end

  vidpids = (ENV["VIDPID"] || DEFAULT_VIDPIDS).downcase.split(",").map(&:strip)
  list = `powershell.exe -Command "usbipd list" 2>&1`
  connected = list.split(/^Persisted:/).first || ""

  targets = connected.lines.filter_map do |line|
    m = line.match(/^(\d+-\d+)\s+(\h{4}:\h{4})\s+(.+?)\s+(Not shared|Shared.*|Attached.*)\s*$/i)
    next nil unless m
    busid, vidpid, device, state = m.captures
    [busid, vidpid.downcase, device.strip, state.strip] if vidpids.include?(vidpid.downcase)
  end
  abort "No connected USB device matches VID:PID #{vidpids.join(', ')} (usbipd list)" if targets.empty?

  targets.each do |busid, vidpid, device, state|
    if state.start_with?("Attached") && ENV["AUTO"] != "1"
      puts "#{busid} #{vidpid} (#{device}): already attached, skipping"
    elsif state == "Not shared"
      puts "#{busid} #{vidpid} (#{device}): NOT SHARED - run once as admin: usbipd bind --busid #{busid}"
    else
      puts "#{busid} #{vidpid} (#{device}): attaching#{ENV['AUTO'] == '1' ? ' (auto-attach, until rake detach)' : ''}..."
      sh usbipd_attach_cmd(busid)
    end
  end
end

desc "Detach USB serial devices from WSL2 back to Windows (e.g. for the web " \
     "installer's WebSerial). Same selection as attach: VID:PID default " \
     "CH340 + CH343; VIDPID=... to override, BUSID=x-y to force one."
task :detach do
  if ENV["BUSID"]
    usbipd_stop_auto_attach(ENV["BUSID"])
    sh "powershell.exe -Command \"usbipd detach --busid #{ENV['BUSID']}\""
    next
  end
  # An auto-attach left running (rake attach AUTO=1) would take the device
  # back right after the detach.
  usbipd_stop_auto_attach

  vidpids = (ENV["VIDPID"] || DEFAULT_VIDPIDS).downcase.split(",").map(&:strip)
  list = `powershell.exe -Command "usbipd list" 2>&1`
  connected = list.split(/^Persisted:/).first || ""

  targets = connected.lines.filter_map do |line|
    m = line.match(/^(\d+-\d+)\s+(\h{4}:\h{4})\s+(.+?)\s+(Not shared|Shared.*|Attached.*)\s*$/i)
    next nil unless m
    busid, vidpid, device, state = m.captures
    [busid, vidpid.downcase, device.strip, state.strip] if vidpids.include?(vidpid.downcase)
  end
  abort "No connected USB device matches VID:PID #{vidpids.join(', ')} (usbipd list)" if targets.empty?

  targets.each do |busid, vidpid, device, state|
    if state.start_with?("Attached")
      puts "#{busid} #{vidpid} (#{device}): detaching..."
      sh "powershell.exe -Command \"usbipd detach --busid #{busid}\""
    else
      puts "#{busid} #{vidpid} (#{device}): not attached, skipping"
    end
  end
end

desc "Detect and cache the correct serial port for #{EXPECTED_CHIP}"
task :"check-port" do
  ports = PROBE_PORTS.select { |p| File.exist?(p) }
  abort "No serial devices found in #{PROBE_PORTS}" if ports.empty?

  puts "Scanning ports for #{EXPECTED_CHIP}..."
  detected = nil

  ports.each do |port|
    print "  Probing #{port}... "
    docker_cmd = [
      "docker run --rm --privileged",
      "--device=#{port}",
      "-v /dev/bus/usb:/dev/bus/usb",
      IMAGE
    ].join(" ")

    output = `#{docker_cmd} esptool.py --port #{port} chip_id 2>&1`
    chip_match = output.match(/Detecting chip type\.\.\.\s*(\S+)/)
    if chip_match
      chip = chip_match[1]
      puts chip
      if chip == EXPECTED_CHIP
        detected = port
        break
      end
    else
      puts "no response"
    end
  end

  if detected
    File.write(PORT_CACHE_FILE, detected)
    puts "#{EXPECTED_CHIP} found on #{detected} (cached to #{PORT_CACHE_FILE})"
  else
    abort "ERROR: #{EXPECTED_CHIP} not found on any port"
  end
end

# The write speed. FLASH_BAUD wins; otherwise a CH34x bridge (CH340 on
# NARYAv3, CH343 on the P4-Nano) gets 115200 from the start -- at the rake
# default of 460800 a long write fails now and then on those, and 115200 has
# not (2026-11-15) -- and anything else idf.py's default (460800). The bridge
# is read from sysfs (USB vendor 1a86); when that cannot be read, the target
# decides (NARYAv3, NARYAv4).
def flash_baud(port)
  baud = ENV['FLASH_BAUD']
  return baud if baud && !baud.empty?
  ch34x_port?(port) ? "115200" : nil
end

def flash_baud_opt(port)
  baud = flash_baud(port)
  baud ? "-b #{baud}" : ''
end

def ch34x_port?(port)
  dev = File.basename(File.realpath(port)) rescue File.basename(port.to_s)
  ["device/../idVendor", "device/../../idVendor"].each do |rel|
    f = "/sys/class/tty/#{dev}/#{rel}"
    return File.read(f).strip.downcase == "1a86" if File.exist?(f)
  end
  %w[NARYAv3 NARYAv4].include?(ENV['FMRB_HW_TARGET'])
end

# Every flash task first checks that build/ was made with the options (the
# board among them) this run asks for (rakelib/build_options.rb): idf.py flash
# builds again with them first, and the other tasks write what build/ holds,
# so a build/ made for another board or with other options stops here,
# before the port is touched. Once per rake run (keep_home and app_sync go
# through flash and flash:app).
task :flash_options do
  esp32_options_guard!('flash', what: "flash")
end

desc "Flash to ESP32 (FLASH_BAUD= overrides the speed; CH34x boards 115200, others 460800)"
task :flash => :flash_options do
  port = get_serial_port
  sh "#{DOCKER_CMD_PRIVILEGED} idf.py -p #{port} #{flash_baud_opt(port)} flash".gsub(/\s+/, ' ')
end

namespace :flash do
  # The storage image (and with it the device's /home) is not written, so this
  # is the loop for iterating on firmware code. It silently leaves the storage
  # partition stale: if flash/ or config/ changed, use the full `rake flash`.
  # PORT=/dev/ttyACMx flashes that port instead of the cached one (rake
  # regress:boot finds its board by VID:PID and passes it here).
  desc "Flash only the app partition (keeps storage incl. the device's /home; PORT= overrides .serial_port)"
  task :app => :flash_options do
    port = ENV['PORT'] && !ENV['PORT'].empty? ? ENV['PORT'] : get_serial_port
    sh "#{DOCKER_CMD_PRIVILEGED} idf.py -p #{port} #{flash_baud_opt(port)} app-flash".gsub(/\s+/, ' ')
  end

  # Flashing without losing the board's /home (doc/dev_remote_ctl/plan.md,
  # "the development build"; doc/remote_guard G0). The WiFi halves are
  # ../tools/fmrb_flash_keep.rb, shared with the MCP flash tool; they need a
  # development build on the board (FMRB_DEV_REMOTE_CTL) both before and after.
  # Build first (rake build:esp32): the storage tree sent or written is
  # build/storage_root as the last build left it.

  desc "Full flash that keeps the board's /home: back it up over WiFi (keys too, into ~/asterism_keys), flash, write it back, reboot (HOST=ip|fmruby-xxxxxx.local, BACKUP_DIR=)"
  task :keep_home => :flash_options do
    host = flash_keep_host
    flash_keep_need_storage_root
    dir = ENV['BACKUP_DIR'] && !ENV['BACKUP_DIR'].empty? ? ["--dir", File.expand_path(ENV['BACKUP_DIR'])] : []
    t0 = Time.now
    out = flash_keep_tool("prepare", host, "--storage-root", FLASH_KEEP_ROOT, *dir)
    backup = out[/^backup=(.+)$/, 1] or abort "the backup did not say where it went"
    name = out[/^name=(.+)$/, 1]
    t1 = Time.now
    Rake::Task[:flash].invoke
    t2 = Time.now
    flash_keep_tool("finish", host, "--dir", backup, *(name ? ["--name", name] : []))
    puts format("keep_home: backup %ds, flash %ds, write-back and reboot %ds; /home is saved in %s",
                t1 - t0, t2 - t1, Time.now - t2, backup)
  end

  # An app-only flash writes neither the partition table nor the P4's font
  # partition (doc/flash_layout F4), so first the board's are compared with
  # the build's; when they differ this stops and says to use keep_home.
  desc "App-only flash, then send what differs between build/storage_root and the board over WiFi; /home untouched; stops when the board's partition table or font image is not the build's; lists the system's files the build no longer has (HOST=, CONF=1 sends /etc/system_conf.toml too, PRUNE=1 deletes those files)"
  task :app_sync => :flash_options do
    host = flash_keep_host
    flash_keep_need_storage_root
    flash_keep_tool("layout", host, "--build", File.expand_path("build"))
    t0 = Time.now
    name = flash_keep_tool("name", host, fail_ok: true)[/^name=(.+)$/, 1]
    Rake::Task["flash:app"].invoke
    t1 = Time.now
    conf = ENV['CONF'] == "1" ? ["--conf"] : []
    # Files of the system's part (/usr, /help, /lib, /boot, /bin,
    # /cache, the /app folders the image has) that the new image does not
    # have are listed at the end; PRUNE=1 deletes them. /home, /app/usr,
    # /app/test, /mnt, /tmp, /var, /etc and what config/storage_exclude_*.txt
    # leaves out are never touched (../tools/fmrb_rd_fs.rb).
    prune = ENV['PRUNE'] == "1" ? ["--prune"] : []
    flash_keep_tool("sync", host, "--storage-root", FLASH_KEEP_ROOT,
                    *(name ? ["--name", name] : []), *conf, *prune)
    puts format("app_sync: flash %ds, sync %ds", t1 - t0, Time.now - t1)
  end

  # The serial way, for a board WiFi does not bring back with an empty /home
  # (no /etc/wifi.toml in the image): the storage partition read with esptool
  # (or the /home of a WiFi backup, BACKUP=), opened with littlefs-python, its
  # /home put into the new storage tree, and that image written in place of
  # build/storage.bin. Everything in the container the build uses, with the
  # build's own littlefs venv.
  desc "Full flash keeping /home over serial: read storage, put its /home into the new image, write (BACKUP=dir uses a backup's /home instead of reading; STORAGE_ONLY=1 writes only storage; BACKUP_DIR=, DEVICE=<machine>-<mac>, READ_BAUD=, PORT=)"
  task :keep_home_serial => :flash_options do
    require "json"
    require "fileutils"
    flash_keep_need_storage_root
    fa = JSON.parse(File.read("build/flasher_args.json"))
    offset = fa.dig("storage", "offset") or abort "build/flasher_args.json names no storage partition"
    chip = fa.dig("extra_esptool_args", "chip")
    size = File.size("build/storage.bin")
    name_max = File.read("sdkconfig")[/^CONFIG_LITTLEFS_OBJ_NAME_LEN=(\d+)/, 1] || "64"
    port = ENV['PORT'] && !ENV['PORT'].empty? ? ENV['PORT'] : get_serial_port
    old_umask = File.umask(0o077)
    if ENV['BACKUP'] && !ENV['BACKUP'].empty?
      dir = File.expand_path(ENV['BACKUP'])
      home_rel = "home"
      abort "#{dir}/home is not a directory" unless File.directory?(File.join(dir, home_rel))
    else
      dir = flash_keep_serial_dir
      FileUtils.mkdir_p(dir, mode: 0o700)
      read_baud = ENV['READ_BAUD'] && !ENV['READ_BAUD'].empty? ? ENV['READ_BAUD'] : "460800"
      sh "#{flash_keep_docker_priv(dir)} esptool.py --chip #{chip} -p #{port} -b #{read_baud} " \
         "read_flash #{offset} #{size} /backup/storage.bin"
      sh "#{flash_keep_docker_lfs(dir)} extract --block-size=4096 --name-max=#{name_max} " \
         "/backup/storage.bin /backup/extract"
      home_rel = "extract/home"
      FileUtils.mkdir_p(File.join(dir, home_rel))
    end
    merged = File.join(dir, "merged_root")
    FileUtils.rm_rf(merged)
    FileUtils.cp_r(FLASH_KEEP_ROOT, merged)
    FileUtils.rm_rf(File.join(merged, "home"))
    FileUtils.cp_r(File.join(dir, home_rel), File.join(merged, "home"))
    sh "#{flash_keep_docker_lfs(dir)} create /backup/merged_root /backup/storage_keep_home.bin " \
       "--fs-size=#{size} --name-max=#{name_max} --block-size=4096"
    baud = flash_baud(port) || "460800"
    if ENV['STORAGE_ONLY'] == "1"
      sh "#{flash_keep_docker_priv(dir)} esptool.py --chip #{chip} -p #{port} -b #{baud} " \
         "--before default_reset --after hard_reset write_flash #{offset} /backup/storage_keep_home.bin"
    else
      args = File.read("build/flash_args").lines.map do |l|
        l.split.first == offset ? "#{offset} /backup/storage_keep_home.bin\n" : l
      end
      File.write(File.join(dir, "flash_args_keep_home"), args.join)
      sh "#{flash_keep_docker_priv(dir, workdir: '/project/build')} esptool.py --chip #{chip} -p #{port} " \
         "-b #{baud} --before default_reset --after hard_reset write_flash @/backup/flash_args_keep_home"
    end
    system("chmod", "-R", "go-rwx", dir)
    puts "keep_home_serial: done; the storage image read, the merged tree and the image written are in #{dir}"
  ensure
    File.umask(old_umask) if old_umask
  end
end

FLASH_KEEP_ROOT = "build/storage_root"
FLASH_KEEP_TOOL = File.expand_path("../tools/fmrb_flash_keep.rb", PWD_)

def flash_keep_host
  host = ENV['HOST'] || ENV['IP']
  abort "say which board: HOST=<ip> or HOST=fmruby-xxxxxx.local (its WiFi address)" if host.nil? || host.empty?
  host
end

def flash_keep_need_storage_root
  return if File.directory?(FLASH_KEEP_ROOT) && File.exist?("build/flasher_args.json")
  abort "no ESP32 build here (#{FLASH_KEEP_ROOT}, build/flasher_args.json): rake build:esp32 first"
end

# Run the WiFi tool, its output shown as it comes and returned. Exit 3 is
# "the board did not come back on WiFi"; the tool has said what to do.
def flash_keep_tool(cmd, *args, fail_ok: false)
  out = +""
  IO.popen([RbConfig.ruby, FLASH_KEEP_TOOL, cmd, *args], err: [:child, :out]) do |io|
    io.each_line { |l| print l; out << l }
  end
  return out if $?.success? || fail_ok
  abort "fmrb_flash_keep.rb #{cmd} failed (exit #{$?.exitstatus})"
end

# Where the serial way keeps what it read: next to the WiFi backups.
def flash_keep_serial_dir
  return File.expand_path(ENV['BACKUP_DIR']) if ENV['BACKUP_DIR'] && !ENV['BACKUP_DIR'].empty?
  device = ENV['DEVICE']
  if (device.nil? || device.empty?) && ENV['HOST'] && !ENV['HOST'].empty?
    out = flash_keep_tool("name", ENV['HOST'], fail_ok: true)
    mac = out[/^name=\S*-(\h{6})$/, 1]
    machine = { "NARYAv4" => "p4nano", "NARYAv3" => "naryav3" }.fetch(ENV['FMRB_HW_TARGET'], ENV['FMRB_HW_TARGET'].to_s.downcase)
    device = "#{machine}-#{mac}" if mac
  end
  abort "say where the image goes: BACKUP_DIR=<dir>, or DEVICE=<machine>-<MAC last 3> " \
        "(e.g. p4nano-xxxxxx) for ~/asterism_keys/devices/<DEVICE>/" if device.nil? || device.empty?
  File.expand_path("~/asterism_keys/devices/#{device}/home_backup_serial_#{Time.now.strftime('%Y%m%d-%H%M%S')}")
end

def flash_keep_docker_priv(dir, workdir: nil)
  w = workdir ? "-w #{workdir}" : ""
  DOCKER_CMD_PRIVILEGED.sub(/ #{Regexp.escape(IMAGE)}\z/, " -v #{dir}:/backup #{w} #{IMAGE}")
end

def flash_keep_docker_lfs(dir)
  ["docker run --rm", USER_OPT, "-e HOME=/tmp", "-v #{PWD_}:/project", "-v #{dir}:/backup",
   "--entrypoint /project/build/littlefs_py_venv/bin/littlefs-python", IMAGE].join(" ")
end

desc "Check ESP32 HW"
task :check do
  sh "#{DOCKER_CMD_PRIVILEGED} esptool.py -p #{get_serial_port} flash_id"
end

desc "Open menuconfig"
task :menuconfig do
  term = ENV['TERM'] || 'xterm-256color'
  docker_cmd_interactive = [
    "docker run --rm -it",
    USER_OPT,
    "-e HOME=/tmp",
    "-e TERM=#{term}",
    "-v #{PWD_}:/project",
    IMAGE
  ].join(" ")
  sh "#{docker_cmd_interactive} idf.py menuconfig"
end


desc "Serial monitor"
task :monitor do
  sh "#{DOCKER_CMD_INTERACTIVE} idf.py -p #{get_serial_port} monitor"
end
