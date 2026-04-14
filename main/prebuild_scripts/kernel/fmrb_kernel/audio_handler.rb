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
    Log.info("Audio command '#{cmd}' from pid=#{pid}")

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
      # Build binary: cmd_type=0x08 (PLAY_SLOT) + music_id(4 LE)
      bin = "\x08\x00\x00\x00\x00"
      bin.setbyte(1, slot & 0xFF)
      bin.setbyte(2, (slot >> 8) & 0xFF)
      bin.setbyte(3, (slot >> 16) & 0xFF)
      bin.setbyte(4, (slot >> 24) & 0xFF)
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
end
