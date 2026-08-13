# 実装計画書: FFT バックエンド選択 gem + Tab5 マイク可視化

別セッションが実装するための手順書。方針の全体像は同ディレクトリの `plan.md`。
本書は「どのファイルに何を足すか」まで落とす。作業は develop から作業ブランチを
切って行い、レポートは `doc/mic_spectrum/report/` に随時記録すること。

- 勝手に git 操作しない(commit/push はユーザの明示指示を待つ)。
- コメントは英語、コミュニケーションは日本語、絵文字なし。
- submodule 直接編集禁止。編集は `lib/add|patch|replace` 経由。sdkconfig 触らない。
- lib/ を編集したら `rake clean`、linux⇔esp32 切替は `rake clean_all`。
- `rake build:linux` は stale な esp32 build/ で偽グリーンを出す →
  検証時は `file build/fmruby-core.elf` で x86-64 を確認。
- 比較は**同一コミットのビルド同士**、report に環境とコミットを明記
  (memory `feedback_same_commit_comparison`)。

---

## 設計の核: アプリは mruby、FFT だけ実行時にバックエンド選択

ビルドを分けるのではなく、**mruby アプリから実行時に FFT の実装を選ぶ**。
`:ruby / :c / :dsp / :spinel` を1つの gem の統一 API で切り替えられる。

```ruby
fft = Fmrb::Fft.new(size: 512, backend: :spinel)   # :ruby|:c|:dsp|:spinel
mag = fft.forward(samples_i16)                      # 統一 I/O(int16 byte 列)
r   = Fmrb::Fft.bench(size: 512, iters: 100, backend: :c)  # => {us_avg:, us_min:, mag:}
```

### 4バックエンドの実行のされ方

| backend | 実装 | 計時 |
|---------|------|------|
| `:ruby` | gem mrblib の純 Ruby FFT(mruby VM で実行) | Ruby 内で K 回ループを計時 |
| `:c`    | C 関数(素朴 radix-2) | K 回を C 内で回し μs を返す |
| `:dsp`  | C 関数(esp-dsp `dsps_fft2r`) | 同上。**P4 実機のみ**(Linux は NA) |
| `:spinel` | **Spinel-AOT の FFT を同居インスタンスで呼ぶ(新パターン)** | K 回を Spinel entry 内で回し μs |

`:ruby` と `:spinel` は **同一の FFT ソース** `fft_core.rb` を使う(公平性)。
`:ruby` はそれを mrblib として mruby でバイトコード実行、`:spinel` は同じ
ファイルを Spinel で C に変換して native 実行。差が出るのは実行エンジンだけ。

### 新パターン(:spinel)の要点 — 確認済みの成立条件

- Spinel は `--no-main --entry <name>_entry` で **`int <name>_entry(void)`** を吐く
  (`compile_ruby_to_spinel.cmake`)。C から呼べる。
- Spinel インスタンスは per-task: `fmrb_spinel_instance_begin(pool, size, gc_th,
  str_th)` が専用プールに作って呼び出しタスクの current にする →
  entry を呼べる → `fmrb_spinel_instance_end`(`fmrb_spinel_host.h`)。
- mruby の `mrb_state` と Spinel インスタンスは別オブジェクト → **mruby アプリの
  タスク内に Spinel インスタンスを同居**させ、FFT entry をライブラリのように呼ぶ。
- 入出力は entry が `void` 引数なので **FFI グローバルバッファ経由**(`:str`/`:ptr`/
  `:binstr`。Float は境界に渡さない)。K 回数も FFI グローバルで渡し、ループは
  Spinel 側に置く(C の計時と揃える)。
- Spinel コンパイラは **`vendor/spinel`**(version-pinned、`rake spinel:setup` が
  SPINEL_PIN のコミットを clone/build。gitignored)。docker には無いので **.c は
  ホストで生成**するが、`rake spinel:gen` はビルド時に any_spinel なら自動実行される
  (`rakelib/build.rake`)ので、実装セッションが自前で生成できる。初回のみ
  `rake spinel:setup` を実行。fft_core.rb を変えたら再生成が要る。

---

## 共通の C 実装(全バックエンドの土台)

`main/kernel/fmrb_fft_bench.c` / `.h`(新規、mrb 非依存の純 C ABI)。mruby binding・
Spinel FFI・マイクタスクの全部から呼べるように1本にまとめる。

- `uint32_t fmrb_fft_c(const int16_t* in, int n, int iters, int16_t* mag_out);`
  素朴 radix-2 を `iters` 回。合計 μs を返し、最後の振幅(int16 スケール)を書く。
- `uint32_t fmrb_fft_dsp(const int16_t* in, int n, int iters, int16_t* mag_out);`
  esp-dsp。`#if !defined(CONFIG_IDF_TARGET_LINUX)` 等でガード、Linux は 0 を返す。
- `uint32_t fmrb_fft_micros(void);` ESP32=`esp_timer_get_time()`、
  Linux=`clock_gettime(CLOCK_MONOTONIC)` を μs で。両ターゲット対応をここに1本。
  (既存 `fmrb_spx_board_micros` は Spinel 時のみコンパイルなので流用しない。)
- twiddle は内部で事前計算(呼び出しごとに再計算しない)。入力は int16 固定。

---

## gem 構成: `lib/add/picoruby-fmrb-fft/`(新規 mrbgem)

- `mrbgem.rake` / `mrblib/fmrb-fft.rb`: `Fmrb::Fft` 本体。`backend:` で分岐。
  `:ruby` は `fft_core.rb` を include。統一 I/O(int16 byte String 入出力)。
  `bench` ヘルパ(K 回・min/avg・mag 返却)。
- `mrblib/fft_core.rb`: **純 Ruby FFT**(radix-2, bit-reversal, Hann, 振幅)。
  詰め方は memory 準拠: `while` 優先(`times` の block コスト回避)、block 呼び出し
  回避、要素 swap は一時変数(parallel assign 使わない)、事前計算 twiddle。
  ※ このファイルを Spinel 側とも共有する(単一ソース)。
- `ports/esp32/fft_binding.c`: mruby binding。
  `mrb_define_module_function` で `Fmrb::FftNative.c_run(in_str, n, iters) ->
  [us, mag_str]` / `.dsp_run(...)` / `.micros`。中身は `fmrb_fft_*` を呼ぶだけ。
  `:spinel` 用に `.spinel_begin(pool_kb)` / `.spinel_run(in_str, n, iters) ->
  [us, mag_str]` / `.spinel_end` も定義(下記)。
- 登録は gem 側 init に集約。ESP/FreeRTOS ヘッダを使うので **ビルド対象追加は
  `components/picoruby-esp32/CMakeLists.txt` の `set(PICORUBY_SRCS ...)`**。
- gembox 追加は `lib/add/family_mruby.gembox`(memory
  `feedback_mrbgem_edits_live_in_lib` / `project_picoruby_json_gembox` の作法)。

### :spinel バックエンドの配線

- Spinel ソース: `main/prebuild_scripts/spinel/fft_spinel.rb`(entry ラッパ)+
  `fft_core.rb`(gem と同一)+ `fmrb_fft_ffi.rb`(FFI 宣言)。entry 名は
  `fmrb_fft_spinel_entry`。entry は FFI で入力/K を読み、K 回 FFT し、振幅と μs を
  FFI out へ書く。
- FFI 宣言 `fmrb_fft_ffi.rb`(`fmrb_ffi.rb` の書式):
  ```ruby
  module FftSpx
    ffi_func :fmrb_fft_spx_input,  [:ptr, :ptr, :ptr], :void  # get in-buf/n/iters
    ffi_func :fmrb_fft_spx_output, [:str, :int, :int], :void  # mag bytes, us
  end
  ```
- Spinel FFI shim: `main/kernel/fmrb_spx_fft.c`(mrb 非依存)。gem の C binding が
  セットしたグローバル入力を entry へ渡し、entry が書いた出力を binding が回収。
- 生成: `rake spinel:gen` に `fft_spinel` の
  `generate_ruby_spinel_command(... fmrb_fft_spinel_entry ...)` を追加、
  `prepare_ruby_spinel_source(fft_spinel ...)` で `.c` を build へ。
- **リンク**: この gem を使うビルドでは Spinel runtime が要る。`main/CMakeLists.txt`
  で「gem 有効時に `fmrb_spinel_rt` を `COMPONENT_REQUIRES` に足す + fft_spinel.c を
  `COMPONENT_SRCS` に足す」。既存 `FMRB_ANY_SPINEL` と OR で扱えるよう新フラグ
  (例 `FMRB_FFT_SPINEL=1`、デフォルト on)を1本。
- 実行時: `spinel_begin(pool_kb)` が `fmrb_malloc` でプール確保 →
  `fmrb_spinel_instance_begin` → est を保持。`spinel_run` が entry を呼ぶ。
  `spinel_end` で `fmrb_spinel_instance_end` + free。**プールは小さく**(FFT を
  no-alloc に書けば数十 KB。内蔵 RAM 逼迫に注意 memory `project_internal_ram_budget`)。
- **制約(重要): :spinel は 1 インスタンス・1 タスクのシングルトン**。受け皿の C が
  file-scope static でインスタンスと入出力を保持し、インスタンスは begin したタスクの
  current。**別タスクから同時に :spinel を呼ぶと壊れる**(run グローバルの競合 +
  current でないインスタンス)。所有タスクを 1 つに限定すること。Track B の
  マイクアプリと bench を同時に :spinel で回さない(マイク側は :dsp 既定なので通常は
  問題にならない)。実装済みの注記: `fmrb_fft_spinel.c` 冒頭 static 群、
  `fmrb-fft.rb` の `spinel_open`。

---

## 段階(de-risk 順)

> 進捗: **Stage 1-3 完了(2026-08-13、branch `feature/mic-spectrum-fft`)**。
> 4者を Tab5 実機で計測済み。同居 Spinel インスタンスは成立(フォールバック不要)。
> 結果は `report/track_a.md`。**残りは Stage 4(マイク、実機必須)**。

### Stage 1 — gem 骨格 + :ruby + :c(mruby / Linux)【完了】
- `fmrb_fft_bench.c`(C FFT + micros)、gem 骨格、`fft_core.rb`、mruby binding。
- 固定入力(既知周波数の int16 正弦波)で **:ruby と :c の振幅一致(ピーク bin)**
  を先に確認。次に μs 採取。
- 検証:
  ```
  rake clean && rake build:linux && file build/fmruby-core.elf
  tools/dev_run_check.sh --keep
  # デモアプリ or 起動時ルーチンで実行 → docker logs fmruby_core | grep FFT
  ```

### Stage 2 — :spinel 同居インスタンス(新パターン, Linux)★最重要 de-risk【完了】
- `fft_spinel.rb` + FFI + shim + CMake 配線。初回 `rake spinel:setup`(vendor/spinel
  にコンパイラ用意)、以後ビルドで `rake spinel:gen` が自動生成。
- **最小スパイクを先に**: mruby アプリのタスク内で
  `instance_begin → entry → instance_end` が落ちずに通り、mruby VM と干渉しないこと
  だけを最小コードで確認(FFT 前に "hello" entry 相当で可)。ここが通れば本命。
- 通ったら FFT entry を接続、:c と振幅一致 → Spinel の μs 採取。
- **フォールバック**(同居 inline が不可なら): Spinel の数字だけは従来型
  (別タスクの combined プログラム + メッセージ)で採る。gem の UX は :ruby/:c/:dsp
  のみ実行時切替とし、:spinel は「別ビルド計測」に格下げ。※ report に判断を残す。
- 検証: Linux で :ruby/:c/:spinel の3者一致 + μs。

### Stage 3 — :dsp + Tab5 実機で4者・実行時切替【完了】
- `main/idf_component.yml` に `espressif/esp-dsp` 追加、`fmrb_fft_dsp` 実装(ガード解除)。
- デモアプリにバックエンド選択 UI(ボタン/メニュー)を付け、**実行時に4者切替**して
  μs を画面表示。
- 実機計測(Tab5、fmruby-core 作業ディレクトリ):
  ```
  rake clean_all && rake check-port
  rake build:esp32 && FLASH_BAUD=115200 rake flash
  python3 ../tools/fmrb_serial_capture.py -t 40 boot.log   # 4者の μs 採取
  ```
  ※ Tab5 は USB-Serial-JTAG でボタン不要。DL モード滞留の誤診に注意
  (memory `feedback_tab5_no_dtr_rts_reset`)。UI 操作は remote desktop で
  (memory `project_tab5_remote_ui_control`)。
- 結果を `doc/mic_spectrum/report/track_a.md` に4者 min/avg μs + 環境 + コミットで表に。

### Stage 4 — マイク可視化(= plan.md トラック B)【未着手・実機必須】

マイク HW は未経験路。**推測でドライバを書かず**、B1 で「値が取れる」を実機で確定して
から先へ進む。以下は小段に割る。

**B1 — マイク bring-up(完了条件: 実機で RMS が動く)**
- `main/drivers/audio_p4/` に I2S RX チャネル + ES7210(`esp_codec_dev` の
  `es7210_codec_new`、`ESP_CODEC_DEV_WORK_MODE_ADC`)。TX の作り方は `audio_p4_hw.c`
  が手本。ES7210 は共有内部 I2C(GPIO31/32、display_p4 の I2C サービス)。I2C
  アドレス/初期化は `managed_components/espressif__esp_codec_dev/device/es7210/` 参照。
- **I2S 結線を実機で確定**: TX が I2S_NUM_1。RX は (a) 別ポート I2S_NUM_0 か
  (b) 同一ポート全二重(`i2s_new_channel(&cfg, &tx, &rx)`)。ES8388 と ES7210 が
  同じ I2S かで決まる → **まず別ポート I2S_NUM_0 を試し、ダメなら全二重**。
- `esp_codec_dev_read()` で mono サンプル(例 16kHz / int16)。**RMS/ピークを
  `fmrb_log` に周期出力**し、無音とタップ/発声で値が動くことを確認。ここまでが B1
  の完了条件(FFT につなぐ前)。音の善し悪しはユーザ確認、値が動くかは自律で確認可。

**B2 — スペクトラム経路(C 側)**
- マイクタスク(または audio_p4 タスク)で: サンプル → Hann 窓 → **esp-dsp FFT**
  (gem の `fmrb_fft_dsp` 経路を流用、性能重視)→ M bin(対数グルーピング 32〜64)を
  最新バッファへ(ダブルバッファか mutex で dirty read 防止)。
- link コマンド `FMRB_LINK_AUDIO_GET_SPECTRUM` を追加(既存 audio link に倣う)。
  最新 bin 配列を byte で返すクエリ型(GET_PIXEL と同じ「取得」型)。

**B3 — Ruby API + アプリ**
- `FmrbAudio#mic_enable` / `#mic_disable` / `#spectrum`(最新 bin を byte 取得)。
  **mruby binding と Spinel FFI の両方**に足す(memory `project_ctrl_tab_focus_switch`)。
- `flash/app/demo/mic_spectrum.app.rb`: 毎フレーム `spectrum` → `FmrbGfx.fill_rect`
  で棒グラフ(ピークホールド・色)。※ **FmrbGfx に width/height は無い**
  (report track_a の知見)→ `@user_area_width` / `@window_height` を使う。
  Stage 3 の4者 μs を画面併記してもよい(任意)。

**B4 — 実機確認**
- Tab5 に焼いて remote desktop で起動。既知トーン(スマホ等)を鳴らしピークが動く
  こと、描画で 30fps 維持を確認。IP は毎回 mDNS で取得(固定しない、
  memory `project_tab5_remote_ui_control`)。音の官能はユーザ確認。

**未確定/決める**: サンプルレート・FFT 長・M bin の対数境界・更新レート・平滑化・
ピークホールド減衰。ES7210 の AEC(エコー消去)は使わない(単純キャプチャ)。

---

## 参考(コード位置)

- Spinel entry ABI / 生成: `main/prebuild_scripts/compile_ruby_to_spinel.cmake`
- Spinel インスタンス境界: `components/fmrb_spinel_rt/include/fmrb_spinel_host.h`
  (`instance_begin/end`)、呼び出し例 `main/kernel/fmrb_kernel.c` 599-641
- Spinel FFI の書式: `main/prebuild_scripts/spinel/fmrb_ffi.rb`
- Spinel 最小例: `main/prebuild_scripts/spinel/hello_kernel.rb`
- エンジン CMake: `main/CMakeLists.txt` 55-140(runtime link は `FMRB_ANY_SPINEL`)
- mruby binding 例: `lib/add/picoruby-fmrb-app/ports/esp32/gfx.c`
  (`mrb_define_method`)、ビルド `components/picoruby-esp32/CMakeLists.txt`
- gem 追加の作法: `lib/add/family_mruby.gembox`、Float/Math の Ruby 例
  `lib/add/picoruby-fmrb-app/mrblib/p5.rb`
- 出力オーディオ(マイクの手本): `main/drivers/audio_p4/audio_p4_hw.c`
- ES7210: `managed_components/espressif__esp_codec_dev/device/es7210/`
- 実機/リモート: ルート `CLAUDE.md`「ESP32-S3 実機の自律検証」「Tab5 リモート UI」

## 落とし穴チェックリスト

- [ ] `:ruby` と `:spinel` が同一 `fft_core.rb` を使っているか(公平性)。
- [ ] twiddle を内側ループで再計算していないか(全実装で事前計算)。
- [ ] 4者が同じ入力・同じ FFT 長・同じ K か。ピーク bin 一致(正しさ先、許容誤差明記)。
- [ ] C 関数を Ruby へ出すとき mruby binding と Spinel FFI の**両方**を足したか。
- [ ] Float を FFI 境界に渡していないか(int16/byte buffer/:int/:binstr のみ)。
- [ ] :spinel の同居インスタンスは begin/run/end が対で、プールを free しているか。
      プールサイズは小さいか(内蔵 RAM 逼迫、`M1|` ログで確認)。
- [ ] esp-dsp が Linux ビルドに混入していないか(ガード)。
- [ ] Spinel .c は `rake spinel:gen` で最新か(fft_core.rb 変更時は再生成)。
- [ ] linux⇔esp32 切替で `rake clean_all`、`file build/*.elf` でターゲット確認。
