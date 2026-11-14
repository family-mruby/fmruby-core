# smf2fmsq test fixtures

`ruby verify.rb` converts every `.mid` here and checks the result against the
expected values written below. It exits non-zero on the first difference, so it
works as a regression test after changes to `../smf2fmsq.rb`.

```
ruby verify.rb                 # structural + acoustic checks
FMSQ_PLAYER=/path/to/fmsq_player ruby verify.rb
```

The acoustic checks need `fmsq_player` from `fmrb-audio-tools` (`rake build`
there). When the binary is missing those checks are skipped, not failed.

Regenerate the `.mid` files with `ruby make_test_midi.rb` after editing the
fixture definitions.

## Fixtures and what they pin down

All files use 480 ticks per quarter note. At 120 BPM a quarter note is 0.5 s,
which is 30 FMSQ frames. Frame 0 holds the converter's init writes, so a note
at time 0 lands on frame 1.

| File | Contents | What it checks |
|---|---|---|
| `scale.mid` | C major scale C4-C5, quarter notes, channel 1, velocity 100 | pitch conversion, frame placement, velocity mapping, and the same values measured back out of the rendered WAV |
| `tempo_change.mid` | Eight A4 notes: four at 120 BPM then four at 240 BPM | Set Tempo is followed (frame spacing 30 then 15) |
| `chord.mid` | Channels 1/2/3 sounding together, plus an overlapping note on channel 1 | three voices at once, last-note priority and the fall back to the held note |
| `drums.mid` | GM percussion on channel 10: hi-hats, kick, snare | drum note to noise settings, velocity separation |
| `format0.mid` | Format 0 file with two channels in one track | the other SMF layout parses |

## Expected values

`scale.mid` - one note every 30 frames starting at frame 1. Pitches are the
APU's quantized values, not the ideal equal-tempered ones: the pulse timer is
an integer, so `verify.rb` recomputes `CPU / (16 * (timer + 1))` and compares
against that. Measured from the rendered WAV, all eight notes land within
1 cent of it and each lasts 0.375 s (the fixture releases notes at 3/4 of the
quarter note so consecutive notes can be told apart).

| Note | MIDI | Ideal (Hz) | APU (Hz) | Frame |
|---|---|---|---|---|
| C4 | 60 | 261.63 | 261.36 | 1 |
| D4 | 62 | 293.66 | 293.60 | 30 |
| E4 | 64 | 329.63 | 329.97 | 60 |
| F4 | 65 | 349.23 | 349.57 | 90 |
| G4 | 67 | 392.00 | 392.49 | 120 |
| A4 | 69 | 440.00 | 440.40 | 150 |
| B4 | 71 | 493.88 | 494.96 | 180 |
| C5 | 72 | 523.25 | 522.71 | 210 |

`tempo_change.mid` - frames 1, 30, 60, 90 then 120, 135, 150, 165.

`chord.mid` - pulse1 C5 at frame 1, E5 at frame 15, C5 again at frame 30
(the overlapping note takes the voice and hands it back); pulse2 E4 and
triangle C3 both start at frame 1.

`drums.mid` - hi-hats (noise period 3, short mode) at frames 1, 15, 30, 45,
60, 75, 90, 105; kick (period 13) at 1 and 60; snare (period 8) at 30 and 90.
The kick and hi-hat that fall on the same frame collapse into one hit, because
the APU has a single noise channel; the later write wins.

`format0.mid` - pulse1 at frame 1, pulse2 at frame 30.
