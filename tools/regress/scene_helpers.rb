# frozen_string_literal: true
#
# scene_helpers.rb - stand-ins that more than one scene file needs. Called
# from an app's prelude (in the forked run, before the app is read).
module Regress
  module Helpers
    MIDI_GEMS = %w[picoruby-midi picoruby-midi-mml picoruby-fmrb-midi].freeze

    module_function

    # FmrbMidi::SerialPort (fmrb_midi_serial.c): a MIDI unit on the port,
    # which takes every byte.
    def midi_serial
      port = Class.new do
        def self._open(*) = true
        def self._close = nil
        def self._open? = true
        def self._write(bytes) = bytes.bytesize
      end
      FmrbMidi.const_set(:SerialPort, port)
    end
  end
end
