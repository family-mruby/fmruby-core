# 実装報告 トラック B: Tab5 マイク (Stage 4 段階 1: bring-up)

対象: `impl_plan.md` Stage 4 の段階 1 / 実施: 2026-08-13 /
ブランチ: `feature/mic-spectrum-fft` (コミット前) / 実機: M5Stack Tab5 (ESP32-P4)

## 結果: マイクは動く。しかも「正しい」ところまで自動で取れた

```
I audio_p4: microphone on: ES7210 47160 Hz 16-bit stereo
I audio_p4: mic selftest block 2: n=256 rms=6391 peak=9336
I audio_p4: mic selftest block 3: n=256 rms=10693 peak=12014
   ...
I audio_p4: mic tone check: played 1000 Hz, heard 1013 Hz
I audio_p4: mic tone check: played 2000 Hz, heard 2026 Hz
I audio_p4: microphone off
```

- **本体のスピーカーで既知の音を鳴らし、本体のマイクで拾って、C の FFT に
  かけてピーク周波数を出した**。人が音を出す必要も、測定器も要らない。
- 512 点 / 47160 Hz なので **1 bin = 92.1 Hz**。1000 Hz は bin 11
  (= 1013 Hz)、2000 Hz は bin 22 (= 2026 Hz) が最も近い bin で、**どちらも
  分解能の範囲でぴったり**。マイクの経路・I2S の並び・FFT の向き (bin と
  周波数の対応) が全部合っていることまで同時に言える。

## 確定した配線 (impl_plan の未確定事項だった点)

**マイクはスピーカーと同じ I2S ポートの RX 側 (全二重)**。別ポートではない。

| 項目 | 値 |
|---|---|
| コーデック | ES7210 (2 マイク、I2C 7bit `0x40`、共有内部バス GPIO31/32) |
| I2S | `I2S_NUM_1` の RX 側。MCLK=30 / BCLK=27 / WS=29 は **TX と共有**、DIN=**28** |
| サンプリング | **47160 Hz** (スピーカーと同じ。基板が 1 本のクロックを共有するため選べない) |
| 形式 | 16bit ステレオ。左チャンネルを使う (マイク 2 個はステレオ対) |

- **サンプリングレートは選べない**のが結論。TX が APU 由来の 47160 Hz
  (= 15720 x 3) なので、マイクもそれになる。512 点で 1 bin = 92.1 Hz、
  上は 23.5kHz まで。**棒グラフの可視化には十分、音声の細かい分析には粗い**。
- 別ポートで 16kHz にするには、共有クロックを 2 つ目のコントローラへ
  引き込む必要があり、基板の配線に対して不自然。今回は全二重を採った。

## I2C は esp_codec_dev ではなく直接叩いた (指示書からの変更)

指示書は「esp_codec_dev の es7210 (ADC モード)」だったが、**採らなかった**。
このボードでは **タッチ (GT911) が回り始めると i2c_master 経路が使えない**
(`audio_p4_hw.c` 冒頭と `doc/tab5_i2c_bus_notes.md` の既知事項) ため、
esp_codec_dev の es7210 ドライバ (i2c_master クライアント) は
**実行時のマイク on/off に使えない**。同じファイルの音量調整が既に
`display_p4_i2c_write_reg8` (lgfx の直列化パス) を使っているので、それに
合わせてレジスタを直接書いた。

レジスタ列はボードの純正ソフトが使っているものと同じ (リセット → クロック →
ADC OSR/モード → HPF → MICBIAS → MIC1/2 ゲイン 0x1B → MIC3/4 停止)。
**触ったことのないハードで自作の初期化列を試さない**、という判断。

## 作ったもの

| ファイル | 追加 |
|---|---|
| `main/drivers/audio_p4/audio_p4_hw.c` | RX チャンネル (全二重) / ES7210 の on/off / `audio_p4_mic_read` / self test / `audio_p4_mic_peak_hz` (C FFT でピーク周波数) |
| `main/drivers/audio_p4/audio_p4_internal.h` | 上記の宣言 |
| `main/drivers/audio_p4/audio_p4_task.c` | bring-up の self test と tone check (`FMRB_MIC_SELFTEST`、**既定 off**) と起動音 |

- **既存のスピーカー経路は壊していない**。全二重の確保に失敗しても TX だけで
  続行し、マイクは "unavailable" になる (フォールバックあり)。
- RX は**要求されるまで初期化も enable もしない**。マイクを使わない起動は
  マイク対応前と同じ経路を通る。

## 起動音まわりで分かった 3 件 (2 件は自分のバグ、1 件は欠落機能)

音の調査は 3 つに分かれた。**「ピコが鳴らない」は自分の回帰ではなく、
Modern にその実装が最初から無かった**のが答え。

### (a) 全二重が成立せず MCLK が張り替わる (自分のバグ。実害あり)

音が変だという報告から I2S の実装を読み直して見つけた。**聴感上の症状が
どれだったかに関わらず、これは実際に壊れている経路**だったので直した。

`i2s_std.c` の `s_i2s_channel_try_to_constitude_std_duplex()` は、TX と RX の
**`i2s_std_config_t` 全体を `memcmp` で比較**し、**完全一致したときだけ**
`full_duplex = true` にする。gpio_cfg も比較対象。

最初の実装は RX 側だけ `dout` を外して `din` を足した別 config を渡していた。
→ memcmp 不一致 → **全二重として認識されない** → 続く `i2s_std_set_gpio()` の

```c
if (!handle->controller->full_duplex) {
    ... i2s_ll_mclk_bind_to_rx_clk(dev);   // MCLK を RX クロックへ張り替える
}
```

が走り、**MCLK (GPIO30) が RX クロックに移る**。その RX はマイクを使うまで
disable のままなので、**ES8388 のマスタークロックが止まる**。音が出ない/
おかしくなるのはこれ。

直し方: **両チャンネルに同一の config を渡す** (`dout` と `din` の両方を
書いた 1 つの config)。ドライバは方向ごとに必要なピンだけ結線するので実害は
無く、これで memcmp が一致して全二重が成立する。

再発防止に**初期化直後の表明**を入れた。`i2s_channel_get_info()` の
`pair_chan` は全二重が成立したときだけ非 NULL なので、そのまま assert に
なる:

```
I audio_p4: i2s full duplex constituted: yes
```

`NO` が出たらスピーカーのクロックが危ない、と一目で分かる。

教訓: **IDF の「全二重」は宣言ではなく、2 つの設定が一致した結果として
成立する**。片方だけ書き換えた config を渡すと、エラーにならずに黙って
簡易二重として扱われ、クロックの張り替えという遠い場所に副作用が出る。

さらに **RX の初期化自体を遅らせた** (`mic_init_rx`)。チャンネルの確保だけ
起動時に行い、クロックと GPIO を書くのはマイクを最初に使うとき。
**マイクを使わない起動は、マイク対応前と完全に同じ経路**を通る。

### (b) tone check が起動音のチャンネルを奪っていた (自分のバグ)

**ユーザ報告「起動時のピコっていう音が変な感じになった」**。原因は自分の
tone check。

- 起動音は system_desktop の `BOOT_EVENTS` が **APU のチャンネル 0-3**
  (P1/P2/Tri/Noise) で鳴らす C→F→G の和音 (約 0.5 秒)。
- tone check は**同じチャンネル 0 (PULSE1)** に 1000Hz / 2000Hz を約 0.43 秒
  ずつ流し、しかも**音声タスクの最初のフレームから**始まる。真っ向から重なり、
  メロディ (C5→F5→G5) が私のトーンで上書きされていた。
- おまけに `audio_p4_engine_note_on` は副作用で FMSQ 再生を止める。

直し方: **`FMRB_MIC_SELFTEST` を既定 off に**した (用は済んだ計装)。
再度必要なら `-DFMRB_MIC_SELFTEST=1` で入る。そのときも重ならないよう、
tone check は起動から **180 フレーム (3 秒) 待ってから**始まるようにした。

教訓: **APU のチャンネルは共有資源**。C 側から音を出す計装は、Ruby 側
(desktop/アプリ) が鳴らしている最中かどうかを考えないと、静かに他人の音を
壊す。ログには何も出ない種類の壊れ方なので、耳で気づくまで分からない。

### (c) 起動の「ピコ」は Modern に無かった (欠落機能。実装した)

Retro では **graphics-audio が自発的に鳴らしている** (`graphics_task.cpp` の
`play_boot_beep`: PC-98 風に 880Hz 80ms → 30ms 休 → 1760Hz 60ms、PULSE1)。
**Modern はこれを継承していなかった** -- `git log -S "boot_beep"` は履歴ゼロ、
`display_p4_task.cpp` に note_on の呼び出しも無い。APU がこちらの
ファームに移った時に落ちたものと思われる。

音声タスクのフレームループに同じ形で実装した (`boot_beep_tick`)。
delay で書き下せないのは、**APU はこのループが回らないと音を出さない**ため。
起動から約 200ms で終わり、これは「Waiting for kernel...」の表示中で、
デスクトップの起動ジングルが同じチャンネルを使うより十分前。
**実機で鳴ることを確認済み**。

## 分かった癖 (次の段階で効く)

- **電源投入直後の 2 ブロックは 0**、そのあと数ブロックは MICBIAS の充電で
  レベルが大きく揺れる (rms 6391 → 11926 → 4908 と減衰)。**可視化アプリは
  最初の 100ms 程度を捨てる**こと。
- `i2s_channel_read` は 256 フレーム (約 5.4ms) 単位。60fps の 1 フレーム
  (16.7ms) に 3 ブロック入る計算。
- tone check は 60Hz ループの中で数フレームに分けて実行している
  (APU はループが回らないと音を出さないため、1 関数の中で鳴らして録るのは
  不可能)。

## 残り (Stage 4 の段階 2 以降)

- **Ruby API**: `FmrbAudio#mic_enable` / `#spectrum` (mruby binding + Spinel
  FFI の両方)。C 側の関数は用意済みなので、あとは公開の仕方。
- **デモアプリ** `flash/app/demo/mic_spectrum.app.rb`: 棒グラフ + ピーク
  ホールド。FFT は C か esp-dsp (トラック A の実測から、Spinel/mruby は
  実時間には遅い)。
- **30fps 維持の実測** (remote desktop)。
