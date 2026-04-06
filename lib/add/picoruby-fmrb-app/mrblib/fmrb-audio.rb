# Family mruby OS - Audio API
# Provides audio control for apps via kernel message routing.

class FmrbAudio
  def initialize(app)
    @app = app
  end

  def play(music_id = 0)
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "play", "music_id" => music_id})
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
end
