# Audio Handler module for FmrbKernelImpl
# Audio message forwarding to host task

module AudioHandlerMixin
  def handle_audio_message(msg)
    data_binary = msg[:data]
    pid = msg[:src_pid]

    begin
      data = MessagePack.unpack(data_binary)
    rescue => e
      Log.error("Failed to unpack audio message: #{e}")
      return
    end

    unless data.is_a?(Hash) && data.key?("cmd")
      Log.error("Invalid audio message format")
      return
    end

    cmd = data["cmd"]
    if cmd == "note_on" || cmd == "note_off"
      # Remember who is playing notes so the voices can be released if that
      # app dies without cleaning up after itself (see silence_notes_for).
      if cmd == "note_on"
        @audio_note_pids = {} unless @audio_note_pids
        @audio_note_pids[pid] = true
      end
      Log.debug("Audio command '#{cmd}' from pid=#{pid}")
    else
      Log.info("Audio command '#{cmd}' from pid=#{pid}")
    end

    # Forward audio command to host task as raw binary
    # Format: cmd_type(1) + path_len(2, LE) + path
    case cmd
    when "play"
      path = data["path"] || ""
      track = data["track"] || 0
      # Build binary: cmd_type=0x02 (PLAY) + path_len(2 bytes LE) + path + track(1)
      path_len = path.length
      bin = "\x02\x00\x00" + path + "\x00"
      bin.setbyte(1, path_len & 0xFF)
      bin.setbyte(2, (path_len >> 8) & 0xFF)
      bin.setbyte(3 + path_len, track & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    when "stop"
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, "\x03")
    when "pause"
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, "\x04")
    when "resume"
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, "\x05")
    when "load_fmsq"
      slot = data["slot"] || 0
      fmsq_data = data["data"] || ""
      # Build binary: cmd_type=0x01 (LOAD_BINARY) + music_id(4 LE) + data_size(4 LE) + data
      data_len = fmsq_data.length
      bin = "\x01\x00\x00\x00\x00\x00\x00\x00\x00" + fmsq_data
      bin.setbyte(1, slot & 0xFF)
      bin.setbyte(2, (slot >> 8) & 0xFF)
      bin.setbyte(3, (slot >> 16) & 0xFF)
      bin.setbyte(4, (slot >> 24) & 0xFF)
      bin.setbyte(5, data_len & 0xFF)
      bin.setbyte(6, (data_len >> 8) & 0xFF)
      bin.setbyte(7, (data_len >> 16) & 0xFF)
      bin.setbyte(8, (data_len >> 24) & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    when "play_slot"
      slot = data["slot"] || 0
      instance = data["instance"] || 0
      # Build binary: cmd_type=0x08 (PLAY_SLOT) + music_id(4 LE) + instance(1)
      # Older audio handlers stop one byte short and treat the payload as
      # MAIN-only; the trailing instance byte is forwards-only metadata.
      bin = "\x08\x00\x00\x00\x00\x00"
      bin.setbyte(1, slot & 0xFF)
      bin.setbyte(2, (slot >> 8) & 0xFF)
      bin.setbyte(3, (slot >> 16) & 0xFF)
      bin.setbyte(4, (slot >> 24) & 0xFF)
      bin.setbyte(5, instance & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    when "load_fmsq_file"
      # Tell the audio task to read an FMSQ from its own LittleFS path
      # instead of sending the bytes inline. Use this when the FMSQ would
      # exceed the IPC payload limit; the file must have been pushed via
      # @gfx.transfer_file first.
      # Binary: cmd_type=0x0B (LOAD_FMSQ_FILE) + music_id(4 LE) + path_len(2 LE) + path
      slot = data["slot"] || 0
      path = data["path"] || ""
      path_len = path.length
      bin = "\x0B\x00\x00\x00\x00\x00\x00" + path
      bin.setbyte(1, slot & 0xFF)
      bin.setbyte(2, (slot >> 8) & 0xFF)
      bin.setbyte(3, (slot >> 16) & 0xFF)
      bin.setbyte(4, (slot >> 24) & 0xFF)
      bin.setbyte(5, path_len & 0xFF)
      bin.setbyte(6, (path_len >> 8) & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    when "note_on"
      ch = data["ch"] || 0
      freq = data["freq"] || 440
      vol = data["vol"] || 10
      duty = data["duty"] || 2
      sweep = data["sweep"] || 0
      # Build binary: cmd_type=0x09 + ch(1) + freq(2 LE) + vol(1) + duty(1) + sweep(1)
      bin = "\x09\x00\x00\x00\x00\x00\x00"
      bin.setbyte(1, ch & 0xFF)
      bin.setbyte(2, freq & 0xFF)
      bin.setbyte(3, (freq >> 8) & 0xFF)
      bin.setbyte(4, vol & 0xFF)
      bin.setbyte(5, duty & 0xFF)
      bin.setbyte(6, sweep & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    when "note_off"
      ch = data["ch"] || 0
      # Build binary: cmd_type=0x0A + ch(1)
      bin = "\x0A\x00"
      bin.setbyte(1, ch & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    else
      Log.warn("Unknown audio command: #{cmd}")
    end
  end

  # Release the note voices an app left sounding.
  #
  # note_on writes APU registers directly and holds the note until a matching
  # note_off, so an app that dies mid-note (an exception skips on_destroy)
  # leaves the sound on with nobody able to stop it. Called from
  # cleanup_terminated_app for any app that ever played a note.
  #
  # Only that app's own kind of traffic is affected: apps that never sent a
  # note_on are not tracked, so a well-behaved player is not cut off by an
  # unrelated app exiting.
  def silence_notes_for(pid)
    return unless @audio_note_pids && @audio_note_pids[pid]

    @audio_note_pids.delete(pid)
    ch = 0
    while ch < 4
      bin = "\x0A\x00"
      bin.setbyte(1, ch)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
      ch += 1
    end
    Log.info("Silenced note voices left by pid=#{pid}")
  end
end
