# Editor read / replace probe (doc/editor_multi_file M3, plan 4.5).
#
# Asks the running editor for its current document the way the AI helper
# will (doc/ai_coding/report/a0.md 3.2) and logs every answer with "AIPROBE:".
# Keys, with this window in front:
#
#   r  ai_read: the editor writes /tmp/ai_doc.rb and answers ai_doc
#   p  ai_replace with the version of the last read: /tmp/ai_new.rb is the
#      document read last with "# AI: test" put on top
#   a  both: read, and replace as soon as the answer comes
#   d  ai_read three seconds from now (time to open a menu or help there)
#   e  ai_replace three seconds from now
#
# A replace after typing in the editor (r, type, p) is refused with "ver".
class AiEditorProbeApp < FmrbApp
  DOC = "/tmp/ai_doc.rb"
  NEW = "/tmp/ai_new.rb"
  DELAY_TICKS = 30   # on_update runs every 100 ms

  def on_create
    @id = 0
    @ver = -1
    @auto = false
    @wait = 0
    @wait_cmd = ""
    @lines = ["r read  p replace  a both", "d / e: the same in 3 s"]
    draw_screen
  end

  def editor_pid
    procs = FmrbApp.ps
    return -1 unless procs
    i = procs.size - 1
    while i >= 0
      pr = procs[i]
      return pr[:id] if pr[:name] == "FM-Editor"
      i -= 1
    end
    -1
  end

  def say(text)
    Log.info("AIPROBE: #{text}")
    @lines << text
    @lines.shift while @lines.size > 8
    draw_screen
  end

  def draw_screen
    clear_user_area
    y = 2
    @lines.each do |l|
      @gfx.draw_text(2, y, l, theme_fg)
      y += 10
    end
    draw_window_frame
    @gfx.present
  end

  def send_read
    pid = editor_pid
    return say("no editor running") if pid < 0
    @id += 1
    send_message(pid, FmrbConst::MSG_TYPE_APP_CONTROL,
                 { "cmd" => "ai_read", "re" => FmrbApp.pid, "id" => @id })
    say("ai_read id=#{@id} -> pid #{pid}")
  end

  def send_replace
    pid = editor_pid
    return say("no editor running") if pid < 0
    return say("read first (no version yet)") if @ver < 0
    body = File.open(DOC, "r") { |f| f.read } || ""
    File.open(NEW, "w") { |f| f.write("# AI: test\n" + body) }
    @id += 1
    send_message(pid, FmrbConst::MSG_TYPE_APP_CONTROL,
                 { "cmd" => "ai_replace", "re" => FmrbApp.pid, "id" => @id,
                   "ver" => @ver, "p" => NEW, "y" => 1, "x" => 0 })
    say("ai_replace id=#{@id} ver=#{@ver}")
  end

  def on_control(msg)
    cmd = msg["cmd"]
    return unless cmd == "ai_doc" || cmd == "ai_done"
    size = MessagePack.pack(msg).bytesize
    if msg["ok"] != 1
      say("#{cmd} id=#{msg["id"]} refused why=#{msg["why"]} ver=#{msg["ver"]} (#{size}B)")
      @auto = false
      return
    end
    if cmd == "ai_doc"
      @ver = msg["ver"]
      on_disk = File.exist?(DOC) ? File.size(DOC) : -1
      body = File.open(DOC, "r") { |f| f.read } || ""
      nl = body.index("\n")
      first = nl ? body[0, nl] : body
      say("ai_doc id=#{msg["id"]} ver=#{@ver} n=#{msg["n"]} b=#{msg["b"]} disk=#{on_disk} " \
          "y=#{msg["y"]} x=#{msg["x"]} sy=#{msg["sy"]} sx=#{msg["sx"]} m=#{msg["m"]} " \
          "f=#{msg["f"]} rp=#{msg["rp"]} (#{size}B)")
      say("first line: #{first}")
      if @auto
        @auto = false
        send_replace
      end
    else
      say("ai_done id=#{msg["id"]} ver=#{msg["ver"]} (#{size}B)")
    end
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    ch = ev[:character] || 0
    if ch == 114      # r
      send_read
    elsif ch == 112   # p
      send_replace
    elsif ch == 97    # a
      @auto = true
      send_read
    elsif ch == 100 || ch == 101   # d / e
      @wait = DELAY_TICKS
      @wait_cmd = ch == 100 ? "read" : "replace"
      say("#{@wait_cmd} in 3 s")
    end
  end

  def on_update
    if @wait > 0
      @wait -= 1
      if @wait == 0
        @wait_cmd == "read" ? send_read : send_replace
      end
    end
    100
  end
end
