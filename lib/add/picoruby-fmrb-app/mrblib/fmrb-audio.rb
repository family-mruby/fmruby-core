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

  def play_slot(slot_id)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "play_slot", "slot" => slot_id})
  end

  def note_on(channel, freq, volume = 10, duty = 2, sweep = 0)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "note_on", "ch" => channel, "freq" => freq, "vol" => volume, "duty" => duty, "sweep" => sweep})
  end

  def note_off(channel)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "note_off", "ch" => channel})
  end
end
