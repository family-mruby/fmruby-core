# C4c 報告: 同梱の声 (Mei) を 48 kHz で作り、16 kHz に落とす

> 状態: 完了 (検収待ち) | 更新: 2026-10-23 | instruction_c4c.md の実装と確認の記録。fmruby-core 8966f129。open_jtalk を Mei 本来の 48 kHz で鳴らし、Ruby の整数の低域通過フィルタで 16 kHz に落としてから -1 dBFS へ正規化する形にした。親の試作 (mei_H) と正規化前のピーク・RMS が 0.001 dB 以内で一致。止まる条件には当たらなかった

## 1. 原因

Mei (`mei_normal.htsvoice`) は 48 kHz 用に作られた声で、メルケプストラムの周波数を曲げる係数 (全域通過の定数) α=0.55 は 48 kHz を前提にしている。C4・C4b では open_jtalk に `-s 16000 -p 80` を渡して 16 kHz で直接鳴らしていたため、α が 16 kHz に合わず (16 kHz なら 0.42 前後が相当)、響きの形 (フォルマント) がずれてつぶれた。「リーチ」が「ぱーぱ」に聞こえたのはこれ。

## 2. 作り方

1. open_jtalk: `-s`・`-p` を渡さない (声の既定の 48000 Hz・240 点)。`-g 6`。`-r`・`-b` なし、声は mei_normal。`OPTIONS = "-g 6"`。
2. 48 kHz の段で切り詰めを数える (`MahjongVoice.clipped_samples`: +32767 か -32768 の標本の数)。1 つでもあれば道具は止まる (「lower -g」と出す)。
3. `MahjongVoice.decimate_wav`: 48 kHz の 16bit モノラルだけを受け、`decimate` で 16 kHz にして 44 バイトの頭をつけ直す。
   - y[k] = Σ h[i] · x[3k + i − 95] (入力の外は 0)。出力の長さは floor(入力 / 3) (試作と同じ)。
4. 今までどおり `normalize_wav` で語ごとにピーク -1 dBFS。出力は 16-bit モノラル 16000 Hz (アプリ・端末は変えていない)。

## 3. 係数の量子化

- `lowpass_taps`: 試作と同じ浮動小数点の式 (遮断 7200 Hz、191 タップ、Blackman 窓、和で正規化) で求め、各係数を 2^20 倍して `Float#round` (0 から遠い側へ四捨五入) で整数にする。丸めた後の和が 2^20 からずれる分は真ん中のタップに足し、和をちょうど 2^20 にする (直流がそのまま通る)。結果は和 1048576、真ん中 314574、最小 -66010、左右対称。
- 積和は整数 (Ruby の Integer なので桁あふれなし)。最後に (|acc| + 2^19) >> 20 で 0 から遠い側へ四捨五入し、符号を戻して -32768..32767 に切り詰める。
- 浮動小数点が入るのは係数を作る段だけで、Q20 に丸めるので、末尾 1 ビットの違いで結果が変わることは事実上ない。試験で「毎回同じ係数」「同じ入力から同じ出力」を見ている。

## 4. 作り直した声

48 kHz の段 (open_jtalk `-g 6` の直後) では切り詰め 0。一番大きいカンでもピーク 14073 (-7.3 dBFS) で、`-g 6` で余裕がある。

| 語 | 48k ピーク | 長さ | 倍率 | 最大 | 平均 (C4b) | 平均 (今回) |
|---|---|---|---|---|---|---|
| riichi | -9.4 dBFS | 0.800 s | ×2.671 (+8.5 dB) | -1.0 dB | -19.4 dB | -15.9 dB |
| pon | -11.6 dBFS | 0.615 s | ×3.435 (+10.7 dB) | -1.0 dB | -14.9 dB | -15.6 dB |
| chi | -11.0 dBFS | 0.680 s | ×3.228 (+10.2 dB) | -1.0 dB | -19.9 dB | -15.0 dB |
| kan | -7.3 dBFS | 0.655 s | ×2.106 (+6.5 dB) | -1.0 dB | -17.2 dB | -18.6 dB |
| tsumo | -11.5 dBFS | 0.815 s | ×3.371 (+10.6 dB) | -1.0 dB | -15.6 dB | -14.3 dB |
| ron | -11.9 dBFS | 0.600 s | ×3.561 (+11.0 dB) | -1.0 dB | -14.5 dB | -14.8 dB |

最大・平均は ffmpeg の volumedetect。長さは C4b と同じ (open_jtalk の音の長さは標本化周波数によらず 5 ms 単位)。

### 再現 (SHA-256)

`--zip` (手元の zip) で 3 回作った (scratchpad への 1 回目、flash/ への書き出し、scratchpad への 2 回目)。6 つの WAV はすべて一致。CREDITS.md は途中で文の折り返しを直したので、直した後の flash/ と 2 回目で一致を確認した。

```
81bc7ffb9d2a6e5cde6215af366972cc19a980f25c7342aa9866ced9a1cafe9f  riichi.wav
75286790143bd315411f2af9aa3adbff96205b537f389a1cfa4cdae6dac407c0  pon.wav
93b2f28ccf701a6a96293dcd4b497b57f47acf2d3c0a4909ab6aba00764c625f  chi.wav
c30c42c4700abc9ce3a330590069969426d5209195661c578bbfc6505b205656  kan.wav
1910d8150742fc3852f02155efb13c7f5307fc191ad39d9384fce4a7248d7b09  tsumo.wav
099d5b4d923eb711a8263338a12e073356084f30ecdfb9eada55162997124635  ron.wav
0b109ad935b0ce5064842c46f01b66c49d09359b6a3d88344c3c87cb75b733d7  CREDITS.md
```

`rake voice:mahjong_default` (SourceForge からのダウンロード) では作っていない。zip の SHA-256 は道具が確認するので、中身は同じになるはず。

## 5. 試作との差

試作の 48 kHz 版 (meicmp/out48/H、`-g 6`) を `decimate_wav` に通したものと、試作の 16 kHz 版 (meicmp/out16/H、正規化前) を比べた。さらに、それを `normalize_wav` に通したものが今回の出荷物と同じバイト列であることも確認した (道具の 48 kHz の段が試作と同じ)。

| 語 | 標本数 | ピーク (今回 / 試作) | RMS (今回 / 試作) | 標本の最大差 |
|---|---|---|---|---|
| riichi | 12800 | -9.533 / -9.533 dBFS | -24.391 / -24.391 dBFS | 1 |
| pon | 9840 | -11.720 / -11.720 | -26.368 / -26.368 | 1 |
| chi | 10880 | -11.180 / -11.180 | -25.224 / -25.224 | 1 |
| kan | 10480 | -7.470 / -7.470 | -25.109 / -25.109 | 1 |
| tsumo | 13040 | -11.556 / -11.556 | -24.832 / -24.832 | 1 |
| ron | 9600 | -12.031 / -12.031 | -25.788 / -25.788 | 1 |

ピーク・RMS の差は 6 語とも 0.001 dB 未満。係数の量子化による違いは標本で高々 1。

## 6. 文書の直し

- `tools/mahjong_voice_default.rb` の冒頭のコメント: 48 kHz で作って 16 kHz に落とすこと、直接 16 kHz で作らない理由 (α が 48 kHz 用で響きが曲がる) を書いた。
- `CREDITS.md` の作り方: 「open_jtalk `-g 6` at the voice's own 48000 Hz, +6 dB; then brought to 16-bit mono 16000 Hz by a 7200 Hz windowed-sinc low-pass (191 taps, Blackman) keeping every 3rd sample; then each word scaled so its peak is at -1 dBFS」と、48 kHz の声である旨の 1 文。

## 7. 試験

- `rake voice:test`: 135 項目すべて合格 (C4b の 113 から増えた)。足したもの:
  - 間引き: 191 タップ、和が 2^20、左右対称、整数、毎回同じ係数。長さが 1/3 (4800→1600、4802→1600)。1 kHz の正弦波が 0.0 dB で通り、周期 16 標本のまま。10 kHz は -60 dB より下 (振幅 10000 で出力はすべて 0 になる)。直流がそのまま、無音は無音、全振幅でも 16bit に収まる。同じ入力から同じ出力。`decimate_wav` が 16 kHz の WAV を返し、中身が `decimate` と同じ、2 回で同じバイト列、16 kHz の WAV は断る。切り詰めの数え方。
  - 同梱の声: CREDITS.md に 48000 Hz・16000 Hz・低域通過の記述、OPTIONS に `-s`・`-p` が無いこと。
- `rake mahjong:overflow`: 255 枚で失敗 0、警告 55 (C4b と同じ)。
- `rake test`: 通った (終了状態 0)。
- Linux sim・ブラウザ・実機は使っていない。聞こえ方は親とユーザが確認する。

## 8. コミット (develop、push なし)

- `8966f129 tools: the bundled Mei voice is rendered at 48 kHz and decimated to 16 kHz` — `tools/mahjong_voice.rb` (間引き)、`tools/mahjong_voice_default.rb`、同梱の声 6 つと CREDITS.md、`test/mahjong_voice/run.rb`。

この report と指示書はコミットしていない。
