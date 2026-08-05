# picoruby-midi-mml in Family mruby

A partial import of the `picoruby-midi-mml` gem from the Midori project. It
carries the MML parser that `FmrbMidi::MmlPlayer` plays.

Same rule as `picoruby-midi`: the imported files are byte for byte what the
original has, so anything learned here can go back upstream as a patch
rather than as a fork. Check with `md5sum` against
`tmp/midori/mrbgems/picoruby-midi-mml/`.

## What was imported

| File | State |
|---|---|
| `mrbgem.rake` | unchanged |
| `mrblib/midi_mml.rb` (`MIDI::MML::Sequence`) | unchanged |

**No file from the original gem was modified.** The parser depends only on
`picoruby-midi`, which is already imported.

## What was left out, and why

| File | Reason |
|---|---|
| `mrblib/midi_mml_player.rb` (`MIDI::MML::Player`, `CombinedPlayer`) | It is a poll-driven player: `tick(clock)` is called from Midori's `MIDI.start!` bpm loop, and a note sounds when that loop happens to run. Family mruby moved the other way in P7.6 - the player hands timed commands to a C queue and a timer sends them, so a note sounds when the score says rather than when a Ruby loop woke up (doc/midi/report/p7_6.md). Importing this would have meant importing the bpm loop too, and giving up the timing the queue exists to provide. `FmrbMidi::MmlPlayer` replaces it. |
| `example/`, `sig/`, `README.md` | Documentation and examples for the Midori API. The MML strings in `example/play_scale.rb` and `example/two_part.rb` are used as fixtures in `tool/midi/test/mml_test.rb` instead. |

## What the parser gives us

`MIDI::MML::Sequence.new(mml, channel:, velocity:)` parses the string and
exposes `events` (an Array of Hashes with `:type`, `:clock`, `:note`,
`:velocity`, `:channel`) plus `total_length` in clocks. A quarter note is 24
clocks, a whole note 96 (`CLOCKS_PER_WHOLE`).

Those Hashes are fine at parse time but not during playback, where Family
mruby allocates nothing per event (doc/midi/report/p6.md). `MmlPlayer`
converts them into packed Integers once, at load, and plays from that.

The parser has no tempo command: `t120` in an MML string is silently
ignored, so the tempo belongs to the player (`MmlPlayer#bpm=`).
