# Family mruby OS - Audio API
# Provides audio control for apps via kernel message routing.

class FmrbAudio
  # The two APU instances. A long piece of music belongs on MAIN and the short
  # noises on SUB, so an effect never cuts the music.
  MAIN = 0
  SUB = 1

  # Channels, as the APU numbers them. Named here rather than in each app: the
  # Python binding has had them from the start, and an app that spells them 0
  # and 1 reads worse for no reason.
  CH_PULSE1 = 0
  CH_PULSE2 = 1
  CH_TRIANGLE = 2
  CH_NOISE = 3

  def initialize(app)
    @app = app
    # note_on / note_off go through a C builder when the owner is a real
    # FmrbApp (see below). Asked once here rather than per note, and there is
    # a Ruby path for owners that only have send_message (the desktop builds
    # an FmrbAudio on itself, and the host tests use a stub).
    @native_note = app.respond_to?(:_send_audio_note)
  end

  # ---- Microphone (Modern / Tab5 only) ----
  #
  # Unlike everything else here, these do not send a message: the microphone
  # is in this firmware, so the samples come straight back from C (FmrbMic in
  # ports/esp32/app.c). On a machine without one, available? is false and read
  # returns nil -- an app can ask without knowing which machine it is on.
  #
  #   audio.mic_enable
  #   bytes = audio.mic_read(512)          # int16 LE, ready for Fmrb::Fft
  #   mag   = Fmrb::Fft.new(size: 512, backend: :dsp).forward(bytes)

  def mic_available?
    ::FmrbMic.available?
  end

  # Samples per second. Fixed by the hardware (the microphone shares the
  # speaker's clocks), so a spectrum's bin width follows from it.
  def mic_rate
    ::FmrbMic.rate
  end

  def mic_enable(on = true)
    ::FmrbMic.enable(on)
  end

  # `count` int16 samples as a byte String, or nil if they did not arrive.
  def mic_read(count, timeout_ms = 200)
    ::FmrbMic.read(count, timeout_ms)
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

  # ---- WAV playback (Modern only) ----
  #
  # Plays a PCM 16-bit mono WAV (8-48 kHz, up to 2 MB) mixed on top of the
  # APU, so a spoken line or a recorded chime rides over whatever music is
  # going. One clip at a time for the whole machine: starting another
  # replaces it.
  #
  #   audio.play_wav("/usr/share/sounds/sine440_16k.wav")
  #
  # The file is synced to the audio side first, the same as a sprite sheet or
  # an FMSQ. On Modern that is a copy inside the one filesystem, so it is
  # cheap and only happens when the bytes actually differ.
  #
  # Retro does not do this, and is not going to. The audio there lives on the
  # WROVER at the far end of a serial link, so every clip would have to be
  # shipped across it and written to that machine's flash before a note of it
  # could sound -- too slow to be worth it, and hard on a filesystem that is
  # not big. play_wav returns false on Retro without sending anything, so an
  # app can call it unconditionally and fall back on the answer (which is what
  # the hourly chime does: a recording if it can, its note if it cannot).
  WAV_CACHE_DIR = "/cache/audio"

  def play_wav(path)
    unless ::FmrbConst::HW_FAMILY == "modern"
      ::Log.info("play_wav: not supported on this machine (#{path})")
      return false
    end
    dest = path
    if sync_needed?
      dest = wav_cache_path(path)
      unless @app.sync_file(path, dest: dest)
        ::Log.warn("play_wav: cannot put #{path} where the audio side can read it")
        return false
      end
    end
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "play_wav", "path" => dest})
    true
  end

  # Whether the file has to be copied before the audio side can see it.
  #
  # On Modern hardware it does not: the audio driver is this firmware and
  # reads the same filesystem, so the copy was pure cost -- 1.9 s and a
  # rewrite of the flash for every fresh file, both of which show up
  # immediately with anything as big as speech. The simulator looks like a
  # Modern machine but its audio lives in another process with its own
  # storage, so there the transfer is still the only way across.
  def sync_needed?
    ::FmrbConst::PLATFORM != "esp32"
  end

  def stop_wav
    return false unless ::FmrbConst::HW_FAMILY == "modern"
    @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_AUDIO,
      {"cmd" => "stop_wav"})
    true
  end

  # Where a source file lands on the audio side. The source tree is mirrored
  # under /cache/audio rather than flattened, so two files with the same
  # basename in different directories do not fight over one cache entry.
  def wav_cache_path(path)
    path[0] == "/" ? "#{WAV_CACHE_DIR}#{path}" : "#{WAV_CACHE_DIR}/#{path}"
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
