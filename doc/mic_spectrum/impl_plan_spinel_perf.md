# 実装計画書: Spinel 実機性能の切り分け(FFT deep-dive E1 / E4)

> **完了 (2026-08-14)。結果は `report/track_a.md` の「E1 / E4」節。**
> 実機 (Tab5/P4) で **23 倍 = double ソフトフロート税 16.70 倍 x AOT Ruby の
> 取り分 1.41 倍** に分解できた。主仮説どおり、主因は精度。
> E4 は **Spinel が固定小数点で 4.28 倍速くなり (10.04ms → 2.34ms)、
> mruby は逆に 1.57 倍遅くなる**という、エンジンで向きが変わる結果。
> 本書はこの先読む必要はない(手順の記録として残す)。

別セッションが実装するための手順書。背景と現状の数字は `report/track_a.md`
(「追記」節)、方針は `plan.md`。本書は E1(精度の単離)と E4(固定小数点)を
実行可能レベルに落とす。

- 勝手に git 操作しない(commit/push はユーザの明示指示)。コメントは英語、会話は日本語、
  絵文字なし。lib/ 編集後は `rake clean`、linux⇔esp32 切替は `rake clean_all`。
  `file build/fmruby-core.elf` でターゲット確認。**比較は同一コミットのビルド同士**、
  report に環境とコミットを明記(memory `feedback_same_commit_comparison`)。

## 前提(確定事実)

- 実機(Tab5/P4)512点: dsp 354us / c 431us / spinel 9948us / mruby 115559us。
  **Spinel は mruby の 11.6x 速いが C の 23x 遅い**(ホストでは C の 7x)。
- 生成 `fft_spinel.c` は **sp_FloatArray(連続 double)** を使用 → boxing/PSRAM 散在は
  **主因ではない**(否定済)。
- `mrb_float` は **double**、C/esp-dsp は **float32**。**ESP32-P4 の FPU は単精度のみ
  (RV32IMAFC、"D" 無し)→ double はソフトフロート**。実機 elf は single-float ABI で
  ビルドされている(`file build/fmruby-core.elf` が "single-float" と出る)。
- **主仮説: 23倍の主因は double ソフトフロート(実機固有)**。ホスト(x86 は double も
  ハード)では出ないので 7x 止まり。

## 既存の土台(再利用する)

- C FFT: `main/kernel/fmrb_fft_bench.c`(`fmrb_fft_c` は全部 `float`)/ `.h`。
- Ruby FFT(:ruby と :spinel が共有): `lib/add/picoruby-fmrb-fft/mrblib/fft_core.rb`
  (Spinel は `rake spinel:gen` がコピーして AOT。gen は gitignore)。
- gem 分岐: `lib/add/picoruby-fmrb-fft/mrblib/fmrb-fft.rb`(`BACKENDS = [:ruby,:c,:dsp,:spinel]`、
  `run` の case、`bench`)。native binding: `ports/esp32/fft_binding.c`。
- 計測 UI: `flash/app/demo/fft_bench.app.rb`(4者を回して μs 表示)。
- 計時: `fmrb_fft_micros()`(esp_timer / CLOCK_MONOTONIC)。

---

## E1 — 精度の単離(最優先・安い)

### ゴール
23倍を「double ソフトフロート税」と「AOT Ruby オーバヘッド」に分解する。
C を double でも計り、`c`(float32/HW)/ `c64`(double/soft-float)/ `spinel`
(double/soft-float/AOT Ruby)を並べる。
- `c64 / c` = double ソフトフロート税(実機固有)。
- `spinel / c64` = **同一精度での AOT Ruby オーバヘッド(真の Spinel 税)**。

### 実装
1. **`fmrb_fft_c_f64` を追加**(`fmrb_fft_bench.c` / `.h`)。`fmrb_fft_c` の完全な複製で、
   作業配列と twiddle/window を `double`、`cosf/sinf/sqrtf` を `cos/sin/sqrt` に。
   アルゴリズム・スケール・入出力(int16 in / int16 mag out)は**完全同一**にする
   (公平性)。重複を嫌うなら `fft_real_t` typedef + テンプレート的マクロでも可だが、
   まずは素直な複製が読みやすい。
2. **backend `:c64` を公開**。gem に増やすと全アプリの UI に出るので、**まずは
   `fft_bench.app.rb` 限定**で足すのが軽い:
   - `ports/esp32/fft_binding.c` に `FftNative.c64_run(in_str, n, iters)` を追加
     (`fmrb_fft_c_f64` を呼ぶだけ、`c_run` と同型)。
   - `fmrb-fft.rb` の `BACKENDS`/`available?`/`run` に `:c64`(native、常時 available)。
     ※ 迷ったら gem に足してよい(mic_spectrum でも切替できて便利)。
3. **正しさ**: `c64` の振幅が `c` と ±1、ピーク bin 一致を確認(同一入力)。

### 計測
- **Linux(相対の確認)**: `rake clean && rake build:linux`、`fft_bench` を起動して
  c / c64 / spinel / ruby の μs。ホストは double もハードなので **c ≈ c64** のはず
  (ここで大差が出たら実装バグ)。
- **P4 実機(本番)**:
  ```
  rake clean_all && rake check-port
  rake build:esp32 && FLASH_BAUD=115200 rake flash
  python3 ../tools/fmrb_serial_capture.py -t 40 e1.log   # c/c64/spinel/dsp の μs
  ```
  Tab5 は USB-Serial-JTAG でボタン不要(memory `feedback_tab5_no_dtr_rts_reset`)。
  UI 操作は remote desktop(memory `project_tab5_remote_ui_control`)。
- **期待**: 実機で **c64 が c より大幅に遅く(ソフトフロート税)、spinel は c64 の
  数倍程度**(= 真の Ruby 税)。これで「23倍の主因=精度」を定量的に確定。

### 記録
`doc/mic_spectrum/report/track_a.md` に E1 節を追記: c/c64/spinel の min/avg μs、
`c64/c` と `spinel/c64` の比、環境・コミット。

---

## E4 — 固定小数点 Q15(実務の本命・やや大きい)

### ゴール
整数は Spinel で即値(箱なし)+ 整数 ALU で **FPU を使わない**。Q15 版で Spinel が
C にどこまで肉薄するか、mruby がどれだけ化けるかを見せる(plan の「第2幕」)。

### 実装
1. **Q15 Ruby FFT** `lib/add/picoruby-fmrb-fft/mrblib/fft_core_q15.rb`:
   - 入力 int16 → Q15 固定小数点、twiddle も Q15 テーブル(事前計算)、
     バタフライは整数積 + シフト(`(a*b) >> 15`)。**段ごとにスケール(>>1)して
     オーバフロー回避**、最後に振幅(整数 sqrt か近似)。
   - memory の教訓: `while` 優先、block 呼び出し回避、要素 swap は一時変数、
     parallel assign 不可。picoruby の整数は 63bit(MRB_INT64?要確認)だが、積は
     32bit に収まるよう Q15 + シフトで管理。
   - **Spinel の整数即値/オーバフロー挙動を要確認**(sp_types の mrb_int 幅)。
2. **Q15 C FFT** `fmrb_fft_c_q15`(`fmrb_fft_bench.c`)。Ruby と行対応。
3. **backend 公開**: `:c_q15` / `:ruby_q15` / `:spinel_q15`。Spinel 側は
   `fft_core_q15.rb` を entry(`fft_spinel_q15.rb` + FFI)で AOT。`rake spinel:gen` /
   `main/CMakeLists.txt` の配線は既存 `fft_spinel` を手本に複製(FMRB_FFT_SPINEL の枠内)。
   ※ Spinel entry を 2 本持つ形になる。重ければ「Q15 は :ruby/:c のみ、Spinel は E1 の
   結論が出てから」と段階化してよい。
4. **正しさ**: float 版と**ピーク bin 一致 + 振幅は許容誤差**(固定小数点なので ±数)。
   既知トーンで検証。

### 計測・記録
E1 と同じ手順で実機計測。`report/track_a.md` に E4 節。**期待**: `spinel_q15` が
`c_q15` に肉薄(double ソフトフロートの足枷が外れる)、`ruby_q15` も float mruby より
大幅改善。

---

## 順序と結論の描き方

1. **E1 を先に**(安い・決定的)。「23倍の主因は double ソフトフロート、同一精度なら
   Spinel は C の数倍」を数字で出す。
2. **E4 で**「整数化すれば Spinel は C に肉薄・mruby も実用域」を示す。
   → showcase: 「AOT Ruby は速いが、MCU の単精度 FPU で double を使うと soft-float 税を
   払う。固定小数点なら AOT の真価が出る」。embedded_constraints.md の 8.1 に追記済みの
   論点(単精度 FPU の soft-float 税)を数字で裏づける。

## 落とし穴チェックリスト
- [ ] c/c64/(q15) が同一入力・同一 FFT 長・同一 K。ピーク bin 一致(許容誤差明記)。
- [ ] ホストで c ≈ c64(差が出たら double 版のバグ)。
- [ ] esp-dsp は Linux ビルドに混入しない(既存ガード)。
- [ ] Spinel を触ったら `rake spinel:gen`(初回 `rake spinel:setup`)。fft_core*.rb 変更時再生成。
- [ ] 比較は同一コミット。report に環境・コミット。
- [ ] linux⇔esp32 で `rake clean_all`、`file build/*.elf` でターゲット確認。

## 参考(コード位置)
- C FFT: `main/kernel/fmrb_fft_bench.c` / `.h`
- Ruby FFT(共有): `lib/add/picoruby-fmrb-fft/mrblib/fft_core.rb`
- gem 分岐: `lib/add/picoruby-fmrb-fft/mrblib/fmrb-fft.rb`、binding `ports/esp32/fft_binding.c`
- Spinel entry/FFI: `main/prebuild_scripts/spinel/fft_spinel.rb` / `fmrb_fft_ffi.rb`、
  生成 `rake spinel:gen`、配線 `main/CMakeLists.txt`(FMRB_FFT_SPINEL)
- 計測アプリ: `flash/app/demo/fft_bench.app.rb`
- 発表用一次資料: `doc/spinel_aot/embedded_constraints.md`(8.1 に soft-float の論点)
