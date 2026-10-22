# C4b 報告: 宣言の声を大きく、効果音を少し小さく

> 状態: 完了 (検収待ち) | 更新: 2026-10-22 | instruction_c4b.md の実装と確認の記録。fmruby-core cd256eb2 (声の正規化と同梱の声の作り直し) と 6637d55b (効果音の倍率)。声の WAV を語ごとにピーク -1 dBFS へそろえ、麻雀アプリの効果音 (SUB) の音量を 3/4 にした。止まる条件には当たらなかった

## 1. 声の正規化

### 1.1 作り方

- `tools/mahjong_voice.rb` の `MahjongVoice` に足した (`tools/mahjong_voice_default.rb` もこれを使う)。
  - `PEAK_DBFS = -1.0`、`FULL_SCALE = 32768` (ffmpeg の volumedetect と同じ数え方)。
  - `peak_target`: 目標の標本値 = round(32768 × 10^(-1/20)) = **29205** (-0.9997 dBFS)。
  - `data_chunk`: WAV の data チャンクの位置と長さ。
  - `peak`: 16bit の標本の絶対値の最大 (-32768 は 32768 と数える)。
  - `normalize_wav(wav)`: 各標本を `target / peak` 倍する。計算は整数だけ (|s| × target × 2 + peak) / (peak × 2) で、0 から遠い側へ四捨五入、16bit に切り詰める。頭 (RIFF・fmt) はそのまま、data の中身だけ差し替える。無音 (ピーク 0) は触らない。戻り値は [WAV, 倍率, 元のピーク]。
  - 整数演算なので、同じ入力からは必ず同じバイト列になる。正規化済みの WAV をもう一度通しても変わらない。
- ずんだもん (VOICEVOX) の道具: 合成 → 形式の確認 → 正規化 → 書き出し。出力の行に倍率と dB を出す。flash_local/ とユーザの VOICEVOX には触れていない (作り直しは親)。
- 同梱の声の道具: open_jtalk の `-s 16000 -p 80 -g 12` は変えず (どの語も切れない。一番大きいカンで -3.7 dBFS)、その後に Ruby で正規化する。`CREDITS.md` の作り方の行に「then each word scaled so its peak is at -1 dBFS」を足した。CREDITS.md の SHA-256 (zip と声のファイル) は元のまま正しい。

### 1.2 同梱の声 (Mei) の倍率

| 語 | 元のピーク (標本) | 元の最大 | 倍率 | dB |
|---|---|---|---|---|
| riichi | 9595 | -10.7 dBFS | ×3.0438 | +9.67 |
| pon | 11584 | -9.0 dBFS | ×2.5211 | +8.03 |
| chi | 10101 | -10.2 dBFS | ×2.8913 | +9.22 |
| kan | 21448 | -3.7 dBFS | ×1.3617 | +2.68 |
| tsumo | 12619 | -8.3 dBFS | ×2.3144 | +7.29 |
| ron | 10813 | -9.6 dBFS | ×2.7009 | +8.63 |

ずんだもんは指示書の測定 (最大 -16.1 dBFS) からすると +15 dB 前後になるはず (語ごとに違う)。

### 1.3 作り直した同梱の声 (ffmpeg volumedetect)

| 語 | 最大 | 平均 (前) | 平均 (後) |
|---|---|---|---|
| riichi | -1.0 dB | -29.1 dB | -19.4 dB |
| pon | -1.0 dB | -22.9 dB | -14.9 dB |
| chi | -1.0 dB | -29.1 dB | -19.9 dB |
| kan | -1.0 dB | -19.8 dB | -17.2 dB |
| tsumo | -1.0 dB | -22.9 dB | -15.6 dB |
| ron | -1.0 dB | -23.1 dB | -14.5 dB |

6 語ともピークの標本は 29205。大きさ (バイト数・長さ) は変わらない。

### 1.4 再現 (SHA-256)

`rake voice:mahjong_default` (SourceForge からダウンロード) で flash/ に、`--zip` (手元の zip) で別のディレクトリに作り、6 つの WAV と CREDITS.md の SHA-256 がすべて一致した。さらに、元の (正規化前の) 出荷物を `normalize_wav` に通した結果も新しい出荷物と同じバイト列だった。

```
aaa6c4de492327761631f247f1d1ad0bdfe87f440dc7b47675ffdffd566ba246  riichi.wav
d9310f99f423a405e81ba040ef76b5bc6bda0000a0a216e44f35c000de6f7903  pon.wav
eea86d62374c9f816ad0eadad41d43b0b24425603b9ad3291c2f5f4c51d4407c  chi.wav
a3984b91d7ca90ab79713975a1e4d34e3c1c43fec4bd5755f79b6acf3d5c766f  kan.wav
96beaca8fe2dce13cdc061604cad6a1b5872c893f8c1302883a8e00a09aa6d04  tsumo.wav
76fac642584ba5236d9073217ef844b04bff4188d48ce4d80b2383610dca1dee  ron.wav
5228d8c73075e7460d94526d8e595de293b9cfb21e8a91a311e7f619f382848e  CREDITS.md
```

## 2. 効果音の倍率

- `mahjong_sound.rb` に `SE_GAIN_NUM = 3`、`SE_GAIN_DEN = 4` と `snd_se_vol(v)` を足し、`snd_tick` の `note_on` に渡す音量をこれで通す。計算は (v × 3 × 2 + 4) / 8 (四捨五入)。0 は 0 のまま (三角波)、0 より大きいものは少なくとも 1。休符は note_off なので関係しない。BGM (MAIN、`main_volume`) は変えていない。
- 対応表 (0-15 → 後):

| 前 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 後 | 0 | 1 | 2 | 2 | 3 | 4 | 5 | 5 | 6 | 7 | 8 | 8 | 9 | 10 | 11 | 11 |

- 表 SFX で使っている音量は 3-13。消える音符は無い (試験で確認)。
- dB の目安 (APU の 0-15 を振幅に比例とみなす): 3/4 は 20 log10(0.75) = **約 -2.5 dB**。丸めのため実際はよく使う 12→9 で -2.5 dB、15→11 で -2.7 dB、10→8 で -1.9 dB、3→2 で -3.5 dB。音量 1 段は、12 付近で約 0.7 dB、8 付近で約 1.1 dB、4 付近で約 2.3 dB (小さい音量ほど 1 段が大きい)。もう少し下げるなら 2/3 (約 -3.5 dB)、戻し気味なら 5/6 (約 -1.6 dB) が候補。

## 3. 試験

- `rake voice:test`: 113 項目すべて合格 (C4 の 74 から増えた)。足したもの:
  - 正規化: 目標 -1 dBFS と標本 29205、data チャンクの位置、負のピーク・-32768 の扱い、倍率 (ピーク 10000 で ×2.9205、+9.31 dB)、四捨五入の向き、頭が変わらないこと、大きすぎる WAV を下げること (32767 → 29204、-32768 → -29205)、無音は触らないこと、雑音の WAV のピークが -1 dBFS ±0.01 dB になること、同じ入力で同じバイト列・2 回通しても同じ・形式が保たれること。
  - 同梱の声: 6 つともピーク 29205、CREDITS.md に正規化の記述。
  - 効果音: 倍率 3/4、0-15 の対応表 (0→0、1→1、15→11)、SFX の全音符が消えず大きくもならないこと、`snd_tick` が実際に送る音量 (チーの 11・10 → 8・8、カンの 12 → 9、三角波は 0 のまま)。
- `rake mahjong:overflow`: 255 枚で失敗 0、警告 55 (C4 と同じ)。
- `rake test`: 通った (終了状態 0)。
- Linux sim・実機は使っていない。聞こえ方は Tab5 で親とユーザが確認する。

## 4. コミット (develop、push なし)

- `cd256eb2 tools: the mahjong voices are peak-normalised to -1 dBFS` — 2 つの道具、同梱の声 6 つと CREDITS.md、試験の正規化の部分。
- `6637d55b game: the mahjong table's effects play at 3/4 of their volume` — `mahjong_sound.rb` と試験の効果音の部分。

この report と指示書はコミットしていない。
