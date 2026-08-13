# 計画書: Tab5 マイクの周波数分析デモ + FFT エンジン比較

2つのトラックで進める。**A を先に**やる(HW リスクゼロ・headless で数字が出る・
Spinel の見せ場)。B(実機マイク)は後追い。

- **トラック A — FFT エンジン比較ベンチ(HW 不要)**: 同じ FFT を **C(素朴) /
  Spinel / mruby / esp-dsp(手最適化)** の4者で走らせ、速度と正しさを比べる。
  本プロジェクトの主題(Spinel=標準、mruby=互換検証)を数字で見せる showcase。
- **トラック B — マイク可視化(Tab5 実機)**: ES7210 マイクをライブ入力し、
  **esp-dsp の FFT**(性能重視)で周波数スペクトラムを画面に描く。録音はしない。

## 調査結果(現状)

- **マイク = ES7210**(エコー消去つき2マイク、専用 ADC コーデック、I2S 接続)。
  出力の ES8388 とは別チップ。`esp_codec_dev` に ES7210 ドライバあり
  (`es7210_adc.h` / `CONFIG_CODEC_ES7210_SUPPORT`)。I2C は共有内部バス
  (GPIO31/32、display_p4 の I2C サービス経由)。
- **入力経路は未実装**: `audio_p4` は I2S TX のみ(`s_tx_chan`、I2S_NUM_1)、
  ES8388 は DAC モード、`rx_handle = NULL`。ES7210 は未初期化。
- **入力 API は未実装**: `FmrbAudio`(Ruby)は出力のみ(play / note_on / fmsq)。
- **Float / Math は両エンジンで動く**: `p5.rb` が実機で `Math.cos/sin/sqrt` +
  Float を使用。`mruby-math` は全ターゲット同梱、Spinel も同経路。→ Ruby で FFT を
  書ける。
- **esp-dsp(FFT)未導入**。追加するか自前 C FFT。
- **Ruby から使えるマイクロ秒計時が無い**。C 側に `fmrb_spx_board_micros` あり
  → ベンチ用に小さな binding を1本足す(トラック A の前提)。
- sim にマイク HW は無い(トラック A は sim/Linux で回る。B の画面確認は Tab5 実機)。

---

## トラック A — FFT エンジン比較ベンチ

### ゴール

**アプリは mruby、FFT のバックエンドだけ実行時に選べる gem** を用意し、同じ入力に
対して **C(素朴)/ Spinel / mruby / esp-dsp** の4者を切り替えて計測する。
`:ruby` と `:spinel` は**同一の Ruby ソース**(mruby VM 実行 / Spinel AOT 実行)で、
差が出るのは実行エンジンだけ。「同じ Ruby が Spinel でどこまで C に迫るか / mruby と
どれだけ差が付くか / esp-dsp の天井はどこか」を4者で出す。

> 実行時切替の肝は **Spinel-AOT の FFT を gem 経由で mruby タスクから呼ぶ**新パターン
> (同居 Spinel インスタンス)。ビルドを2回に分けない。詳細と成立条件・フォール
> バックは `impl_plan.md`。

### 4人の比較対象(確定)

| # | 対象 | 位置づけ |
|---|------|----------|
| 1 | **C(素朴 radix-2)** | Ruby と 1:1 対応する公平な基準 |
| 2 | **Spinel** | 同じ Ruby ソースを AOT ネイティブで実行 |
| 3 | **mruby** | 同じ Ruby ソースを VM で実行 |
| 4 | **esp-dsp** | Espressif 公式・アセンブラ手最適化 FFT の“天井” |

### 公平さの条件(重要)

- **同一アルゴリズム**: radix-2 Cooley-Tukey、bit-reversal、Hann 窓、振幅算出まで
  3実装で同型。
- **twiddle(sin/cos)は事前計算テーブル**。内側ループで `Math.sin` を呼ばない
  (呼ぶと FFT でなく Math.sin を測ることになる)。3者とも同条件。
- **同じ入力・同じ FFT 長(512)**。合成正弦波(既知周波数)を固定バッファに。
- **正しさを先に担保**: 3実装の振幅スペクトラムが一致し、ピーク bin が入力周波数と
  合うことを確認してから速度を語る。
- 計測: K 回回して μs/回(min / avg)。GC の影響も見えるよう min と avg 両方。
- Ruby は memory の教訓を守って詰める: `while` 優先(`times` の block コスト回避)、
  block 呼び出し回避、要素 swap は一時変数(parallel assign は使わない)。

### 実装

- **まず Float 版**(C↔Ruby が完全同型で最も公平)。mruby の Float は boxing で
  GC が重い ―― それが出るのも正直な結果として見せる。
- **固定小数点(整数)版は第2幕**(伸ばし目標)。整数化で mruby/Spinel がどれだけ
  化けるかを見せる。※ここでは最初の一手には含めない。
- **esp-dsp は比較の4人目に確定**(任意ではない)。`main/idf_component.yml` に
  `espressif/esp-dsp` を追加、`dsps_fft2r`(float radix-2)を同じ入力・同じ FFT 長で。
  esp-dsp は Espressif の実機向けなので数字は **P4 実機で採取**(C/Spinel/mruby は
  Linux でも採れる。実機で4者そろえる)。

### 段階

1. **Ruby FFT + C FFT + 計時 binding**: 同型の radix-2 を Ruby と C で書く。Ruby へ
   μs 計時(`fmrb_spx_board_micros` ラップ)を1本追加。まず **Linux sim** で C/Spinel/
   mruby の出力一致を確認。
2. **Linux 計測**: C / Spinel / mruby の μs/回を採取(ホストなので桁は違うが、相対と
   正しさの確認に足る。esp-dsp はここでは対象外)。
3. **P4 実機計測**: esp-dsp を加えた4者を Tab5 に焼いてシリアルログで μs を採取。
   これが本命の数字。
4. **提示**: 結果を doc/mic_spectrum/report/ に表で記録。デモアプリでは画面併記も。

### 検証

- **正しさ・速度とも Linux headless で確認できる**(合成入力・シリアル/stdout ログ)。
- 実機の数字は Tab5 に焼いてシリアルで採取(画面不要)。

### 未確定 / 実装時に決める

- **:spinel 同居インスタンスの成立**(新パターン)。最小スパイクで先に確認、不可なら
  フォールバック(impl_plan.md Stage 2)。
- FFT 長(256/512/1024)、K 回数、入力波形、窓の有無を比較軸に含めるか。
- 比較基準の C は「素朴版」(Ruby と 1:1)。esp-dsp は手最適化の天井として別枠で。
- :spinel プールサイズ(内蔵 RAM 逼迫に注意)。

---

## トラック B — マイク可視化(Tab5 実機)

### データフロー

```
ES7210 (I2S RX) → [C: 新 mic タスク]
   サンプル取得(mono, 例 16kHz)
   → Hann 窓 → FFT(**esp-dsp**、性能重視)→ 振幅
   → M bin(対数グルーピング, 32〜64 本)→ 最新バッファ
→ link クエリで Ruby アプリが「最新スペクトラム」を取得
→ FmrbGfx で棒グラフ描画(ピークホールド・色)。ベンチ結果(C/Spinel/mruby/esp-dsp μs)を併記
```

- 実時間描画は速い実装(C)で回す。スペクトラムは小さいので **link のクエリコマンド
  で十分**(共有メモリ不要)。GET_PIXEL / editor suggest と同じ「fill してから取得」型。

### 段階

1. **マイク bring-up**: I2S RX + ES7210(esp_codec_dev、ADC モード)。生サンプルの
   RMS/ピークをシリアルログに出し「値が取れる」を最初に確定。I2S 結線(TX と同一
   ポートの全二重か別ポートか)をここで確定。
2. **link + API**: 新コマンド(例 `FMRB_LINK_AUDIO_GET_SPECTRUM`)で最新 bin 配列を
   返す。Ruby 側は `FmrbAudio#mic_enable` / `#spectrum`(または新 `FmrbMic`)。
3. **デモアプリ**: `flash/app/demo/mic_spectrum.app.rb` — 毎フレーム spectrum を
   取得して棒グラフ描画。ベンチ結果を併記。
4. **実機 + 性能**: 更新 fps・描画で 30fps 維持を実測。

### 検証

- **値の正しさ**: 生サンプル RMS / FFT ピーク周波数はシリアルログで数値確認できる
  (既知トーンを鳴らして突き合わせ)。headless で担保。
- **画面表示**: Tab5 実機 remote desktop で確認(sim にマイク HW なし)。

---

## リスク

- **マイク HW は未経験路**(トラック B) → bring-up に実機必須。段階1で早期に
  「値が取れる」を確認して切り分ける。トラック A は HW 無依存なので先行できる。
- 既存 I2S TX とマイク RX の共存(ポート/クロック)。
- 比較の公平性を崩さない(twiddle 事前計算・同型アルゴリズム・出力一致の担保)。
- 性能: マイク可視化で FFT + 描画が 30fps を維持できるか(B 段階4で実測)。

## やらないこと

- 録音・ファイル保存。3マイク以上/本格 AEC。声認識。
- Retro(S3)でのマイク対応(HW 前提。P4/Tab5 限定)。※ FFT 比較ベンチ(A)は
  Linux/実機どちらでも回るので S3 でも計測可能。

## 参考(コード位置)

- 出力の手本: `main/drivers/audio_p4/audio_p4_hw.c`(I2S + esp_codec_dev の作り方)
- ES7210: `managed_components/espressif__esp_codec_dev/device/es7210/`
- Float/Math + 数値計算の Ruby 例: `lib/add/picoruby-fmrb-app/mrblib/p5.rb`
- μs 計時の C 源: `fmrb_spx_board_micros`(`main/kernel/fmrb_spx_common.c`)
- アプリ描画: `FmrbGfx`(fill_rect で棒)、既存デモ `flash/app/demo/`
- エンジン方針: memory `project_engine_policy`(Spinel=標準、mruby=互換検証)
