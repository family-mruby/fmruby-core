# The mahjong table's declarations (bundled voice)

`riichi.wav` `pon.wav` `chi.wav` `kan.wav` `tsumo.wav` `ron.wav`: リーチ・ポン・チー・カン・ツモ・ロン,
played by `flash/app/game/mahjong/mahjong_sound.rb` when someone declares.

- **Voice**: HTS Voice "Mei" (`mei_normal.htsvoice`, version 1.5), released by the
  MMDAgent Project Team (http://www.mmdagent.jp/).
  Copyright (c) 2009-2018 Nagoya Institute of Technology, Department of Computer Science.
  Licensed under the Creative Commons Attribution 3.0 license
  (https://creativecommons.org/licenses/by/3.0/).
- **Source**: MMDAgent_Example-1.8.zip (MMDAgent sample script 1.8, 2018-12-25),
  https://sourceforge.net/projects/mmdagent/files/MMDAgent_Example/MMDAgent_Example-1.8/
  (zip SHA-256 `f702f2109a07dca103c7b9a5123a25c6dda038f0d7fcc899ff0281d07e873a63`,
  `MMDAgent_Example-1.8/Voice/mei/mei_normal.htsvoice` SHA-256 `b19e8e5ddef4d9d9559d654f395818e2bb3c0bbdbbca533f96086de21ac5db2b`).
- **Synthesis**: Open JTalk 1.11 (BSD-3-Clause, Nagoya Institute of Technology) with the
  dictionary open-jtalk-mecab-naist-jdic 1.11 (BSD-3-Clause, Nara Institute of Science and
  Technology and Nagoya Institute of Technology), Debian bookworm packages `1.11-3`.
  The program and the dictionary are not shipped, only the audio they made.
- **Generated, not edited**: rendered by `tools/mahjong_voice_default.rb`
  (open_jtalk `-g 6` at the voice's own 48000 Hz, +6 dB; then brought to 16-bit mono
  16000 Hz by a 7200 Hz windowed-sinc low-pass (191 taps, Blackman) keeping
  every 3rd sample; then each word scaled so its peak is at -1 dBFS). Mei is a
  48 kHz voice: rendered directly at 16 kHz its frequency warping is wrong and the words
  blur. Run it again to reproduce them.

The app shows the credit 「声: Open JTalk (Mei, CC BY 3.0)」 while these are played.
