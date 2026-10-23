# C4c 指示書: 同梱の声 (Mei) を 48 kHz で作り、16 kHz に落とす

> 状態: 指示 | 更新: 2026-10-23 | ブラウザ版で聞いたユーザの感想 (2026-10-23): 「リーチが『ぱーぱ』って聞こえる」。Mei は 48 kHz の声 (周波数を曲げる係数 α=0.55) なのに、open_jtalk に `-s 16000 -p 80` を渡して 16 kHz で直接鳴らしていたため、響きの形がずれてつぶれていた。48 kHz で作ってから Ruby の低域通過フィルタで 16 kHz に落とした試作 (親が作った mei_H) をユーザが聞き、「mei_H_normal でよい」と決めた。C4b の続き (instruction_c4b.md、report/c4b.md)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 決まっている作り方 (ユーザが聞いて選んだ mei_H と同じ)

1. open_jtalk は **`-s` と `-p` を渡さない** (声の既定の 48000 Hz・240 点)。音量は `-g 6` (`-g 12` だと 48 kHz で切り詰めが出るおそれ。切り詰めが出ないことを確認し、出るなら下げる)。声は `mei_normal`、速さは既定 (`-r` なし)、後処理フィルタ (`-b`) なし。
2. Ruby で 48000 → 16000 Hz に落とす: 窓つき sinc の低域通過 (遮断 7200 Hz、191 タップ、Blackman 窓、係数の和で正規化) をかけて 3 つに 1 つを取る。親の試作は下 (浮動小数点):

   ```ruby
   N = 191; fc = 7200.0 / 48000
   H = (0...N).map { |i| m = i - (N - 1) / 2; s = m.zero? ? 2 * fc : Math.sin(2 * Math::PI * fc * m) / (Math::PI * m)
     s * (0.42 - 0.5 * Math.cos(2 * Math::PI * i / (N - 1)) + 0.08 * Math.cos(4 * Math::PI * i / (N - 1))) }
   g = H.sum; H.map! { |x| x / g }
   # y[k] = sum_i H[i] * x[3k + i - (N-1)/2]  (outside the input = 0), rounded and clamped to int16
   ```

   - **決まった入力から決まった出力**を保つため、係数は整数 (例 Q15 か Q20、丸めは決めた方法で) にして整数で積和し、丸めてから切り詰める。係数の量子化で試作と数値がわずかに違うのはよい (ピーク・RMS が試作と ±0.1 dB 以内なら同じ音とみなす)。
3. そのあと今までどおり語ごとにピーク -1 dBFS へ正規化 (`MahjongVoice.normalize_wav`)。出力は 16-bit モノラル 16000 Hz (アプリと端末の側は変えない)。

## 2. 作るもの

1. `tools/mahjong_voice_default.rb`: 上の作り方に改める。間引きは `tools/mahjong_voice.rb` (の `MahjongVoice`) に関数として置いてよい。冒頭のコメント (今は「-s 16000 with the 5 ms frame as -p 80」と書いてある) と `CREDITS.md` の作り方の記述を、48 kHz で作って 16 kHz に落とす形に直す。なぜ直接 16 kHz で作らないか (α が 48 kHz 用なので響きがずれる) を 1〜2 行で残す。
2. `rake voice:mahjong_default` で `flash/usr/share/sounds/voice/mahjong/default/` を作り直す (zip は `--zip` で手元のものを使ってよい: /tmp/claude-1000/-home-kishima-fmrb-family-mruby/ac2e25be-21b2-46ea-8f7c-511bdcdbea0d/scratchpad/mei/MMDAgent_Example-1.8.zip。SHA-256 は道具が確認する)。
3. `rake voice:test` に、間引きの試験を足す (例: 1 kHz の正弦波はほぼそのまま残り、10 kHz の正弦波は -60 dB 以下に落ちる。長さが 1/3。決まった入力から同じ出力)。

## 3. 確認

- `rake voice:test`、`rake mahjong:overflow`、`rake test` が通る。
- 作り直した 6 語の最大・平均の dB (ffmpeg の volumedetect か Ruby)、長さ、SHA-256 を report に。2 回作って SHA-256 が同じ。
- 親の試作と比べる: 試作の 16 kHz 版は /tmp/claude-1000/-home-kishima-fmrb-family-mruby/ac2e25be-21b2-46ea-8f7c-511bdcdbea0d/scratchpad/meicmp/out16/H/ (正規化前)。正規化前どうしでピーク・RMS の差を report に。
- 48 kHz の段で切り詰め (±32767 に張りついた標本) が無いこと。
- Linux sim・ブラウザ・実機は使わない (聞こえ方は親とユーザが確認する)。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の `tools/mahjong_voice.rb`・`tools/mahjong_voice_default.rb`・`flash/usr/share/sounds/voice/mahjong/default/`・`test/mahjong_voice/`・`rakelib/voice.rake`、report。
- **触らないもの**: ユーザの VOICEVOX、flash_local/、ずんだもんの作り方 (`tools/mahjong_voice.rb` の VOICEVOX の経路は変えない)、Tab5・シリアル・sim・ブラウザ、アプリ (`flash/app/`)、端末側の C、scratchpad の試作ファイル (読むだけ)。
- push・タグはしない。`git commit -a` は使わない。件名 `tools:`、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。fmruby-core の doc (この指示書・report) はコミットしない (親がする)。
- **止まる条件**: 再現しない (2 回で SHA-256 が違う)、`-g 6` でも切り詰めが出て音量の決め方を変える必要がある、試作とピーク・RMS が 0.1 dB を超えて違い原因が分からない、範囲外の変更が要る。
- **report** (`doc/commentary/report/c4c.md`、常体): 原因 (α と標本化周波数)、作り方、係数の量子化の方法、作り直した声の dB・長さ・SHA-256、試作との差、試験の結果、コミット。
