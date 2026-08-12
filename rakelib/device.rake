# rakelib/device.rake
# Device I/O: usbip attach/detach, port detect, flash, check, monitor.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.

desc "Attach USB serial devices to WSL2 via usbipd, selected by VID:PID " \
     "(default 1a86:7523 = CH340; VIDPID=xxxx:yyyy[,xxxx:yyyy] to override, " \
     "BUSID=x-y to force one bus id). Attaches every matching device."
task :attach do
  if ENV["BUSID"]
    sh "powershell.exe -Command \"usbipd attach --wsl --busid #{ENV['BUSID']}\""
    next
  end

  vidpids = (ENV["VIDPID"]|| "1a86:7523").downcase.split(",").map(&:strip)
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
      puts "#{busid} #{vidpid} (#{device}): already attached, skipping"
    elsif state == "Not shared"
      puts "#{busid} #{vidpid} (#{device}): NOT SHARED - run once as admin: usbipd bind --busid #{busid}"
    else
      puts "#{busid} #{vidpid} (#{device}): attaching..."
      sh "powershell.exe -Command \"usbipd attach --wsl --busid #{busid}\""
    end
  end
end

desc "Detach USB serial devices from WSL2 back to Windows (e.g. for the web " \
     "installer's WebSerial). Same selection as attach: VID:PID default " \
     "1a86:7523 = CH340; VIDPID=... to override, BUSID=x-y to force one."
task :detach do
  if ENV["BUSID"]
    sh "powershell.exe -Command \"usbipd detach --busid #{ENV['BUSID']}\""
    next
  end

  vidpids = (ENV["VIDPID"] || "1a86:7523").downcase.split(",").map(&:strip)
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

desc "Flash to ESP32 (override baud with FLASH_BAUD=115200 etc; default 460800)"
task :flash do
  baud = ENV['FLASH_BAUD']
  baud_opt = baud && !baud.empty? ? "-b #{baud}" : ''
  sh "#{DOCKER_CMD_PRIVILEGED} idf.py -p #{get_serial_port} #{baud_opt} flash".gsub(/\s+/, ' ')
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
