# MIDI Constants
#
# Standard MIDI message types, control change numbers, and note helpers

module MIDI
  # Channel Voice Messages (status byte high nibble)
  NOTE_OFF          = 0x80
  NOTE_ON           = 0x90
  POLY_AFTERTOUCH   = 0xA0
  CONTROL_CHANGE    = 0xB0
  PROGRAM_CHANGE    = 0xC0
  CHANNEL_PRESSURE  = 0xD0
  PITCH_BEND        = 0xE0

  # System Common Messages
  SYSEX_START       = 0xF0
  MTC_QUARTER_FRAME = 0xF1
  SONG_POSITION     = 0xF2
  SONG_SELECT       = 0xF3
  TUNE_REQUEST      = 0xF6
  SYSEX_END         = 0xF7

  # System Realtime Messages
  TIMING_CLOCK      = 0xF8
  START             = 0xFA
  CONTINUE          = 0xFB
  STOP              = 0xFC
  ACTIVE_SENSING    = 0xFE
  SYSTEM_RESET      = 0xFF

  # USB-MIDI Code Index Numbers
  CIN_MISC              = 0x00
  CIN_CABLE_EVENT       = 0x01
  CIN_SYSCOMMON_2       = 0x02
  CIN_SYSCOMMON_3       = 0x03
  CIN_SYSEX_START       = 0x04
  CIN_SYSCOMMON_1       = 0x05
  CIN_SYSEX_END_2       = 0x06
  CIN_SYSEX_END_3       = 0x07
  CIN_NOTE_OFF          = 0x08
  CIN_NOTE_ON           = 0x09
  CIN_POLY_KEY          = 0x0A
  CIN_CONTROL_CHANGE    = 0x0B
  CIN_PROGRAM_CHANGE    = 0x0C
  CIN_CHANNEL_PRESSURE  = 0x0D
  CIN_PITCH_BEND        = 0x0E
  CIN_SINGLE_BYTE       = 0x0F

  # Common Control Change numbers
  module CC
    BANK_SELECT_MSB   = 0
    MODULATION        = 1
    BREATH            = 2
    FOOT              = 4
    PORTAMENTO_TIME   = 5
    DATA_ENTRY_MSB    = 6
    VOLUME            = 7
    BALANCE           = 8
    PAN               = 10
    EXPRESSION        = 11
    EFFECT_1          = 12
    EFFECT_2          = 13
    GENERAL_1         = 16
    GENERAL_2         = 17
    GENERAL_3         = 18
    GENERAL_4         = 19
    BANK_SELECT_LSB   = 32
    DATA_ENTRY_LSB    = 38
    SUSTAIN           = 64
    PORTAMENTO        = 65
    SOSTENUTO         = 66
    SOFT_PEDAL        = 67
    LEGATO            = 68
    HOLD_2            = 69
    SOUND_VARIATION   = 70
    RESONANCE         = 71
    RELEASE_TIME      = 72
    ATTACK_TIME       = 73
    CUTOFF            = 74
    DECAY_TIME        = 75
    VIBRATO_RATE      = 76
    VIBRATO_DEPTH     = 77
    VIBRATO_DELAY     = 78
    SOUND_CTRL_10     = 79
    GENERAL_5         = 80
    GENERAL_6         = 81
    GENERAL_7         = 82
    GENERAL_8         = 83
    PORTAMENTO_CTRL   = 84
    REVERB_DEPTH      = 91
    TREMOLO_DEPTH     = 92
    CHORUS_DEPTH      = 93
    DETUNE_DEPTH      = 94
    PHASER_DEPTH      = 95
    DATA_INCREMENT    = 96
    DATA_DECREMENT    = 97
    NRPN_LSB          = 98
    NRPN_MSB          = 99
    RPN_LSB           = 100
    RPN_MSB           = 101
    ALL_SOUND_OFF     = 120
    RESET_ALL_CTRL    = 121
    LOCAL_CONTROL     = 122
    ALL_NOTES_OFF     = 123
    OMNI_MODE_OFF     = 124
    OMNI_MODE_ON      = 125
    MONO_MODE_ON      = 126
    POLY_MODE_ON      = 127
  end

  # Note name helpers
  module Note
    C  = 0
    Cs = 1; Db = 1
    D  = 2
    Ds = 3; Eb = 3
    E  = 4
    F  = 5
    Fs = 6; Gb = 6
    G  = 7
    Gs = 8; Ab = 8
    A  = 9
    As = 10; Bb = 10
    B  = 11

    # Convert note name and octave to MIDI note number
    # @param note [Integer] Note within octave (0-11, use Note::C, Note::D, etc.)
    # @param octave [Integer] Octave number (-1 to 9)
    # @return [Integer] MIDI note number (0-127)
    def self.number(note, octave)
      (octave + 1) * 12 + note
    end

    # Get note name string
    # @param midi_note [Integer] MIDI note number (0-127)
    # @return [String] Note name (e.g., "C4", "F#5")
    def self.name(midi_note)
      names = %w[C C# D D# E F F# G G# A A# B]
      octave = (midi_note / 12) - 1
      note = midi_note % 12
      "#{names[note]}#{octave}"
    end
  end
end
