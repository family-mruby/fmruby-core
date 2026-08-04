# Family mruby OS - Audio API
# Provides audio control for apps via kernel message routing.

class FmrbAudio
  def initialize(app)
    @app = app
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
  # every few milliseconds. Their keys are Symbols, unlike every other
  # message here, because a String key is a fresh object at every call - a
  # six-key literal costs eight - and that garbage is what wakes the
  # collector in the middle of a phrase (doc/midi/report/p6.md). Symbols are
  # free to name, and the serializer writes a Symbol exactly as it writes a
  # String, so the bytes on the wire and the kernel side are unchanged.
  def note_on(channel, freq, volume = 10, duty = 2, sweep = 0)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      { cmd: :note_on, ch: channel, freq: freq, vol: volume, duty: duty, sweep: sweep })
  end

  def note_off(channel)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      { cmd: :note_off, ch: channel })
  end
end
