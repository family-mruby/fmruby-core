# Family mruby OS - Audio API
# Provides audio control for apps via kernel message routing.
#
# FmrbAudio is on the app boundary (sig/fmrb_audio.rbs, doc/app_boundary):
# play_raw, stop, pause, resume, nsf_status, load_fmsq, load_fmsq_file,
# play_slot_raw, main_volume=, play_wav, stop_wav, note_on_raw and note_off
# are C (generated in mruby, the Spinel base's for Spinel), and build the
# kernel's command themselves -- no Hash per call. What is here are the
# wrappers with optional arguments, and the microphone.

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

  # The sound is the calling app's (the boundary takes no receiver); app is
  # kept for the code that asks for it.
  def initialize(app)
    @app = app
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
    play_raw(path, track)
  end

  # Where the NSF started with play is, asked of the audio side
  # (doc/nsf_player_v2). An NSF has no length, so the audio side decides when
  # a song is over -- it fell silent, its loop has been heard twice, or it ran
  # into the time limit -- fades it out and stops it. Ask about once a second
  # while a song plays and move on when it is over:
  #
  #   st = @audio.nsf_status
  #   next_song if st && st[:gen] == @play_gen && st[:state] == FmrbAudio::NSF_ENDED
  #
  # A Hash {state:, reason:, track:, laps:, gen:, period:, frames:} or nil when
  # the audio side does not answer (an older graphics-audio firmware). gen
  # counts the play commands the audio side has taken: the question does not
  # wait behind a play just sent, so the first answers after one can still be
  # about the song before it. period and frames are 60 Hz frames.
  NSF_IDLE = 0
  NSF_PLAYING = 1
  NSF_FADING = 2
  NSF_ENDED = 3

  NSF_END_SILENCE = 1
  NSF_END_LOOP = 2
  NSF_END_LIMIT = 3

  # nsf_status, pause, resume, load_fmsq and load_fmsq_file are on the
  # boundary. load_fmsq sends the tune inline, so a short one only (one
  # message); a bigger one is pushed with @gfx.transfer_file and loaded with
  # load_fmsq_file.

  # instance picks the APU: MAIN (0, mixed with NSF) or SUB (1, mixed with
  # note_on/off). Short FMSQ effects go on SUB so a long BGM on MAIN keeps
  # playing.
  def play_slot(slot_id, instance: 0)
    play_slot_raw(slot_id, instance)
  end

  # main_volume= (on the boundary): the MAIN instance's own volume, 0-100 %,
  # for the music played with play / play_slot on MAIN, so a BGM can sit
  # under the effects on SUB. One value for the whole machine; the kernel
  # puts it back to 100 when the app that lowered it ends.
  #
  #   audio.main_volume = 30

  # ---- WAV playback (Modern only) ----
  #
  # Plays a PCM 16-bit mono WAV (8-48 kHz, up to 2 MB) mixed on top of the
  # APU, so a spoken line or a recorded chime rides over whatever music is
  # going. One clip at a time for the whole machine: starting another
  # replaces it.
  #
  #   audio.play_wav("/usr/share/sounds/sine440_16k.wav")
  #
  # The audio side reads the file. On the devices that is the one
  # filesystem; the simulator's audio lives in another process with its own
  # storage, so there the file is synced into /cache/audio first.
  #
  # Retro does not do this, and is not going to. The audio there lives on the
  # WROVER at the far end of a serial link, so every clip would have to be
  # shipped across it and written to that machine's flash before a note of it
  # could sound -- too slow to be worth it, and hard on a filesystem that is
  # not big. play_wav returns false on Retro without sending anything, so an
  # app can call it unconditionally and fall back on the answer (which is what
  # the hourly chime does: a recording if it can, its note if it cannot).
  # play_wav and stop_wav are on the boundary (main/app/fmrb_boundary_audio.c).

  # note_on is the one sent in a stream (a MIDI song sends one every few
  # milliseconds): note_on_raw and note_off build the message in C and
  # allocate nothing, so a collection (100-205 ms on the device, audible)
  # never follows from a note (doc/midi/report/p7.md).
  def note_on(channel, freq, volume = 10, duty = 2, sweep = 0)
    note_on_raw(channel, freq, volume, duty, sweep)
  end
end
