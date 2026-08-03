# picoruby-midi in Family mruby

This is a partial import of the `picoruby-midi` gem from the Midori project
(https://github.com/kirikak2/picoruby-midi). It carries the MIDI protocol
layer that Family mruby's MIDI support is built on.

Keeping the file contents identical matters: the gem is on its way into
upstream PicoRuby, and anything learned here should be returnable as a patch
rather than as a fork.

## What was imported

| File | State |
|---|---|
| `mrbgem.rake` | unchanged |
| `mrblib/midi_constants.rb` | unchanged |
| `mrblib/midi_device.rb` | unchanged |

**No file from the original gem was modified.**

## What was left out, and why

| File | Reason |
|---|---|
| `mrblib/midi.rb` | Only holds factory helpers (`MIDI.usb_host_device`, `MIDI.sam2695_device`, `MIDI.input`, `MIDI.clock`) for transports and classes Family mruby does not have yet. The Family mruby factory lives in `picoruby-fmrb-midi` instead (`FmrbMidi.device`). |
| `mrblib/midi_clock.rb` | `MIDI::Clock` drives a C timer (`MIDI._init_timer` and friends) that is not part of this import. |
| `mrblib/midi_input.rb` | `MIDI::Input` needs the C receive task; Family mruby is send-only for now. |
| `src/`, `ports/`, `include/` | The C layer (parser, scheduler, clock generator, transport op-table). Family mruby's transport is written in Ruby, so none of it is needed yet. |

## Methods that need the C layer

`MIDI::Device#trigger` and `#trigger_batch` call `MIDI._trigger` and
`MIDI._send_batch`, which the C layer normally provides.
`picoruby-fmrb-midi` supplies pure Ruby stand-ins for both so the Midori API
keeps working; see that gem's notes for the difference in behaviour (the
Ruby version needs to be pumped, the C version runs off a timer).

Everything else in `MIDI::Device` - `note_on`, `note_off`, `control_change`,
`program_change`, `pitch_bend`, the CC helpers, the realtime messages and
SysEx - is pure Ruby and works as-is on top of any transport object.

## Transport interface

`MIDI::Device` takes any object that responds to:

```
send_packet(cable, cin, b1, b2, b3)
read_available
bytes_available
connected?
device_info
```

Family mruby's APU transport implements exactly these; see
`lib/add/picoruby-fmrb-midi/mrblib/fmrb-midi.rb`.
