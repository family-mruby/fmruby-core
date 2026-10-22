# C4b 指示書: 宣言の声を大きく、効果音を少し小さく

> 状態: 指示 | 更新: 2026-10-22 | Tab5 で聞いたユーザの感想 (2026-10-22): 「若干効果音が大きく感じた。ボイスはもっと大きいほうがいい」。声の WAV をピークで正規化して大きくし、麻雀アプリの効果音 (APU の SUB) の音量を少し下げる。C4 の続き (instruction_c4.md、report/c4.md)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 今の状態 (2026-10-22 に測った)

- ずんだもんの声 (`rake voice:mahjong`、flash_local/) は最大 -16.1 dBFS・平均 -31.5 dB。小さい。
- 同梱の Mei の声 (`rake voice:mahjong_default`、flash/、open_jtalk の `-g 12`) は最大 -9.0 dBFS・平均 -22.9 dB。
- 効果音・ジングルは `flash/app/game/mahjong/mahjong_sound.rb` の音符の表の音量 (0-15) を `snd_tick` の `@audio.note_on(ch, freq, ms, vol, 0)` でそのまま鳴らしている。

## 2. 作るもの

1. **声の正規化**: 両方の道具 (`tools/mahjong_voice.rb`・`tools/mahjong_voice_default.rb`) が、作った WAV (16bit モノラル) を**ピーク -1 dBFS** にそろえる (6 語とも同じ目標。語ごとに倍率を変えてよい)。Ruby だけで (WAV の data を読み、倍率をかけ、切り詰めて書く)。決まった入力から決まった出力 (同梱の声の作り直しで SHA-256 が揃う) を保つ。倍率は report に。
   - 同梱の声は `rake voice:mahjong_default` で作り直して flash/ を更新し、`CREDITS.md` の作り方の記述 (と SHA-256) を合わせる。
   - ずんだもんは道具の変更だけ (flash_local/ は git に入らない。ユーザの VOICEVOX は使わない: 親が作り直す)。
2. **効果音を少し下げる**: `mahjong_sound.rb` に効果音の倍率の定数 (例 3/4) を 1 つ置き、`note_on` に渡す音量に掛ける (0 は 0 のまま、0 より大きいものは少なくとも 1)。BGM (MAIN) は変えない。倍率は 3/4 を既定とし、report に「1 段 = 約何 dB」を書く。
3. **試験**: `rake voice:test` に、正規化の倍率の計算と、ピークが -1 dBFS 付近になること、効果音の倍率 (0→0、15→11 など) を足す。

## 3. 確認

- `rake voice:test`、`rake mahjong:overflow`、`rake test` が通る。
- 作り直した同梱の声の最大・平均の dB を測って report に (ffmpeg の volumedetect で)。2 回作って同じ SHA-256。
- Linux sim・実機は使わない (聞こえ方は親とユーザが Tab5 で確認する)。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の `tools/mahjong_voice.rb`・`tools/mahjong_voice_default.rb`・`flash/usr/share/sounds/voice/mahjong/default/`・`flash/app/game/mahjong/mahjong_sound.rb`・`test/mahjong_voice/`・`rakelib/voice.rake`、report。
- **触らないもの**: ユーザの VOICEVOX (127.0.0.1:50021)、flash_local/、Tab5・シリアル・sim、asterism-games (実況のページの効果音は今回変えない)、他のアプリ。
- push・タグはしない。`git commit -a` は使わない。件名 `game:` / `tools:`、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。fmruby-core の doc (この指示書・report) はコミットしない (親がする)。
- **止まる条件**: 正規化で同梱の声の作り直しが再現しなくなる、効果音の倍率で音が消える音符が出る (倍率の選び方を選択肢で返す)。
- **report** (`doc/commentary/report/c4b.md`、常体): 正規化の作り方と倍率、作り直した声の dB と SHA-256、効果音の倍率と dB の目安、試験の結果、コミット。
