# What both desktops share beyond the panels (doc/linux_native, L4): the
# mouse desktop (system_desktop.app.rb) and the keyboard-only one
# (system_desktop_keys.app.rb) include this.
#
#   builtin_app_catalog  the built-in apps a desktop offers, with what they
#                        declare about screens and the pointer
#   spawn_app            ask the kernel to start an app (and hand it a file)
#   read_conf_string     one top-level string of system_conf

module DesktopCommonMixin
  # The built-ins' declarations live here, beside their names and icons:
  # a desktop is what offers them to the user, and nothing else reads these.
  # The same two keys an app's .app.toml carries:
  #   screens        the screen forms the app draws on ("426x240", "320x170")
  #   needs_pointer  whether it cannot be used without a mouse or touch
  # An entry without them is offered by the mouse desktop only. Labels are
  # resolved through FmrbI18n when this is called, so they follow the
  # language in system_conf.
  def builtin_app_catalog
    [
      { label: FmrbI18n.t(:shell), app: "default/shell",
        icon_file: "usr/share/icon/shell.icon",
        screens: ["426x240", "320x170"], needs_pointer: false },
      { label: FmrbI18n.t(:editor), app: "default/editor",
        icon_file: "usr/share/icon/editor.icon",
        screens: ["426x240", "320x170"], needs_pointer: false },
      # The store is built in like the other two: it installs apps, so it must
      # not be one of the things it can uninstall. Its .app.toml is gone, so
      # this list is the only place its name and icon now live.
      { label: FmrbI18n.t(:app_store), app: "default/appstore",
        icon_file: "usr/share/icon/appstore.icon",
        screens: nil, needs_pointer: nil },
    ]
  end

  # +open_path+ asks the kernel to hand that file to the new app as soon as it
  # has a queue, as a file_selected control message. The kernel does the
  # waiting (its spawn handler defers the send until the app can receive it),
  # which is why nothing here counts ticks any more.
  def spawn_app(app_name, open_path = nil)
    Log.info("Requesting spawn: #{app_name}#{open_path ? " with #{open_path}" : ""}")
    data = { "cmd" => "spawn", "app_name" => app_name }
    data["open_path"] = open_path if open_path
    success = send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    if success
      Log.info("Spawn request sent successfully")
    else
      Log.error("Failed to send spawn request")
    end
  end

  # One top-level `key = "value"` out of system_conf. No Regexp on this
  # machine, so this walks the lines, the way the launcher reads a .app.toml.
  def read_conf_string(key)
    text = File.open("/etc/system_conf.toml", "r") { |f| f.read }
    return nil unless text
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = lines[i].strip
      i += 1
      next if line.empty?
      next if line.start_with?("#")
      # A section header means the top-level keys are behind us.
      break if line.start_with?("[")
      eq = line.index("=")
      next unless eq
      next unless line[0, eq].strip == key
      v = line[eq + 1, line.length - eq - 1].strip
      # Trim a trailing comment before the quotes are taken off.
      hash = v.index("#")
      v = v[0, hash].strip if hash && hash > 0
      len = v.length
      v = v[1, len - 2] if len >= 2 && v[0] == "\"" && v[len - 1] == "\""
      return v
    end
    nil
  rescue => e
    Log.warn("could not read #{key} from system_conf: #{e.message}")
    nil
  end
end
