# Family mruby OS - General MIDI instrument names
#
# The sound set every GM instrument agrees on, so an app can offer "pick an
# instrument" without inventing its own list. Program numbers are as MIDI
# sends them (0-127); the numbers printed on synthesizers are these plus one.
#
#   FmrbMidi.gm_name(0)    # => "Ac Grand Piano"
#   device.program_change(0, channel: 0)
#
# Names are abbreviated to sixteen characters so they fit a line on the
# device screen next to the controls that change them. Where the official
# name is longer the abbreviation is the obvious one ("Acoustic Grand Piano"
# -> "Ac Grand Piano"); nothing here is a different instrument from the GM
# list, only a shorter way of writing it.
#
# This is data, not behaviour: it costs one Array and 128 short Strings per
# VM and is never touched while a song plays.
#
# Channel 10 (channel 9 as MIDI counts) is percussion in GM and its program
# numbers mean drum kits rather than instruments, so an app that offers this
# list should leave that channel alone.

module FmrbMidi
  GM_NAMES = [
    "Ac Grand Piano", "Bright Piano", "El Grand Piano", "Honky-tonk Piano",
    "El Piano 1", "El Piano 2", "Harpsichord", "Clavi",
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
    "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Perc Organ", "Rock Organ", "Church Organ",
    "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    "Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar",
    "Muted Guitar", "Overdrive Gtr", "Distortion Gtr", "Gtr Harmonics",
    "Acoustic Bass", "Finger Bass", "Pick Bass", "Fretless Bass",
    "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    "Violin", "Viola", "Cello", "Contrabass",
    "Tremolo Strings", "Pizzicato Str", "Orchestral Harp", "Timpani",
    "Str Ensemble 1", "Str Ensemble 2", "Synth Strings 1", "Synth Strings 2",
    "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
    "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
    "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute",
    "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Square Lead", "Saw Lead", "Calliope Lead", "Chiff Lead",
    "Charang Lead", "Voice Lead", "Fifths Lead", "Bass + Lead",
    "New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad",
    "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
    "FX Rain", "FX Soundtrack", "FX Crystal", "FX Atmosphere",
    "FX Brightness", "FX Goblins", "FX Echoes", "FX Sci-Fi",
    "Sitar", "Banjo", "Shamisen", "Koto",
    "Kalimba", "Bag Pipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
    "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    "Gtr Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
    "Telephone Ring", "Helicopter", "Applause", "Gunshot"
  ]

  # The channel GM reserves for percussion, as MIDI counts channels.
  GM_DRUM_CHANNEL = 9

  class << self
    # Name of a GM program, or nil outside 0-127.
    def gm_name(program)
      return nil if program.nil? || program < 0 || program > 127

      GM_NAMES[program]
    end
  end
end
