# frozen_string_literal: true
#
# Apps that take a stream or put one out: walk_scroller driven by a step
# sender on Asterism (counterpart :steps), the MIDI APU demo out of the
# serial MIDI port into the monitor (counterpart :midi), and the same demo
# on the built-in sound read from the APU's ring (counterpart :audio).

Regress::Live.scene "walk_scroller" do |s|
  st = s.counterpart :steps
  s.launch "/app/demo/walk_scroller.app.rb"
  s.wait_log(%r{walk_scroller: \S+/walk connected}, timeout: 20)
  st.walk 10, cadence: 120
  shown = st.state["shown"]
  s.check "walked", { "sent" => st.state["sent"], "shown" => shown.size,
                      "last_count" => shown.last&.at(1), "marks" => shown.map { |x| x[2] }.uniq }
  s.park
  s.shot "walked", quiet: 2.5, timeout: 15
end

# The scale: C4 to C5 on channel 1, each note 200 ms and 60 ms apart
# (midi_apu.app.rb SCALE_NOTE_MS, SCALE_GAP_MS).
Regress::Live.scene "midi_serial" do |s|
  m = s.counterpart :midi
  s.launch "/app/demo/midi_apu.app.rb"
  s.wait_log(/MidiApu: device=/, timeout: 15)
  s.pause 500
  s.key "7" # out: serial (the port opens once per boot: no log line to wait for)
  s.wait_change
  s.key "1" # the scale
  notes = s.poll("the scale's 8 notes at the monitor", timeout: 10) do
    n = m.notes
    n.size >= 8 && n.all? { |x| x[2] } && n
  end
  lengths = notes.map { |_, on, off| ((off - on) * 1000).round }
  steps = notes.each_cons(2).map { |a, b| ((b[1] - a[1]) * 1000).round }
  median = ->(a) { a.sort[a.size / 2] }
  before = m.messages.take_while { |x| x[:kind] != :note_on }
  s.check "scale", {
    "notes" => notes.map { |n, _, _, ch| "#{n}/ch#{ch}" },
    # the app's all_off before it plays: all notes off (CC 123) and all
    # sound off (CC 120) on the 16 channels
    "all_off_first" => before.last(32).size == 32 && before.last(32).all? { |x| x[:kind] == :control },
    "note_ms" => (median.call(lengths) / 10.0).round * 10,
    "step_ms" => (median.call(steps) / 10.0).round * 10,
    # each length and spacing within 40 ms of the app's
    "steady" => lengths.all? { |x| (x - 200).abs <= 40 } && steps.all? { |x| (x - 260).abs <= 40 }
  }
end

Regress::Live.scene "apu_sound" do |s|
  a = s.counterpart :audio
  s.launch "/app/demo/midi_apu.app.rb"
  s.wait_log(/MidiApu: device=/, timeout: 15)
  s.pause 500
  quiet = a.listen(1.0)
  loud = a.listen(3.0) { s.key "1" }
  s.check "scale", {
    "silent_before" => quiet["sounding"].zero?,
    "sounded" => loud["peak"] > 256,
    # sounding windows (50 ms) as seconds: the scale sounds for 8 x 260 ms
    "sounding_s" => (loud["sounding"] * Regress::Counterparts::Audio::WINDOW_S).round
  }
end
