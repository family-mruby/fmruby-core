# Family mruby OS - Audio API
# Provides audio control for apps via kernel message routing.

class FmrbAudio
  def initialize(app)
    @app = app
    # note_on / note_off go through a C builder when the owner is a real
    # FmrbApp (see below). Asked once here rather than per note, and there is
    # a Ruby path for owners that only have send_message (the desktop builds
    # an FmrbAudio on itself, and the host tests use a stub).
    @native_note = app.respond_to?(:_send_audio_note)
  end

  def play(path, track: 0)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "play", "path" => path, "track" => track})
  end

  def stop
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "stop"})
  end

  def pause
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "pause"})
  end

  def resume
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "resume"})
  end

  def load_fmsq(slot_id, binary_data)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "load_fmsq", "slot" => slot_id, "data" => binary_data})
  end

  # Load an FMSQ slot from a file already present on the graphics-audio side
  # (push it there first with @gfx.transfer_file). Use this when the FMSQ
  # data would exceed the inline IPC payload limit of load_fmsq.
  def load_fmsq_file(slot_id, path)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "load_fmsq_file", "slot" => slot_id, "path" => path})
  end

  # Play a previously-loaded FMSQ slot. instance picks the APU instance:
  # 0 = MAIN (mixed with NSF), 1 = SUB (mixed with note_on/off SE). Use
  # SUB for short FMSQ sound effects so a long BGM on MAIN keeps playing.
  def play_slot(slot_id, instance: 0)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "play_slot", "slot" => slot_id, "instance" => instance})
  end

  # These two are the only ones called in a stream: a MIDI song sends one
  # every few milliseconds. That makes their garbage matter - on the device a
  # collection stops the app for 100-205 ms, long enough to hear
  # (doc/midi/report/p7.md) - so the message is built in C and this path
  # allocates nothing at all.
  #
  # The Ruby fallback below is the same message written as a Hash. Its keys
  # are Symbols rather than Strings because a String key is a fresh object at
  # every call; the serializer writes a Symbol exactly as it writes a String,
  # so both paths and the kernel side see the same bytes.
  def note_on(channel, freq, volume = 10, duty = 2, sweep = 0)
    if @native_note
      @app._send_audio_note(true, channel, freq, volume, duty, sweep)
    else
      @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
        { cmd: :note_on, ch: channel, freq: freq, vol: volume, duty: duty, sweep: sweep })
    end
  end

  def note_off(channel)
    if @native_note
      @app._send_audio_note(false, channel, 0, 0, 0, 0)
    else
      @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
        { cmd: :note_off, ch: channel })
    end
  end
end
